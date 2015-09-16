// Copyright 2015 Rocana, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "median.h"
#include <assert.h>
#include <sstream>

#include <boost/random/ranlux.hpp>
#include <boost/random/uniform_int.hpp>

using namespace impala_udf;
using namespace std;

using boost::ranlux64_3;
using boost::uniform_int;

// Utility function for serialization to StringVal from Cloudera sample code
template <typename T>
StringVal ToStringVal(FunctionContext* context, const T& val) {
  stringstream ss;
  ss << val;
  string str = ss.str();
  StringVal string_val(context, str.size());
  memcpy(string_val.ptr, str.c_str(), str.size());
  return string_val;
}

template <>
StringVal ToStringVal<DoubleVal>(FunctionContext* context, const DoubleVal& val) {
  if (val.is_null) return StringVal::null();
  return ToStringVal(context, val.val);
}

// Approximate median based on reservoir sampling. Adapted from Impala's
// built-in APPX_MEDIAN function. This version takes a second parameter
// that is the maximum number of samples to cap memory usage.
// Detailed differences are called out in the comments below.
const static int MAX_STRING_SAMPLE_LEN = 10;

template <typename T>
struct ReservoirSample {
  // Sample value
  T val;
  // Key on which the samples are sorted.
  double key;

  ReservoirSample() : key(-1) { }
  ReservoirSample(const T& val) : val(val), key(-1) { }

  // Gets a copy of the sample value that allocates memory from ctx, if necessary.
  T GetValue(FunctionContext* ctx) { return val; }
};

// Template specialization for StringVal because we do not store the StringVal itself.
// Instead, we keep fixed size arrays and truncate longer strings if necessary.
template <>
struct ReservoirSample<StringVal> {
  uint8_t val[MAX_STRING_SAMPLE_LEN];
  int len; // Size of string (up to MAX_STRING_SAMPLE_LEN)
  double key;

  ReservoirSample() : len(0), key(-1) { }

  ReservoirSample(const StringVal& string_val) : key(-1) {
    len = min(string_val.len, MAX_STRING_SAMPLE_LEN);
    memcpy(&val[0], string_val.ptr, len);
  }

  // Gets a copy of the sample value that allocates memory from ctx, if necessary.
  StringVal GetValue(FunctionContext* ctx) {
    StringVal result = StringVal(ctx, len);
    memcpy(result.ptr, &val[0], len);
    return result;
  }
};

template <typename T>
struct ReservoirSampleState {
  // In our fork of this function, the samples are in a dynmaiclly
  // allocated array rather than statically allocated.
  ReservoirSample<T>* samples;

  // Maximum number of samples. New to our fork.
  int max_samples;

  // Number of collected samples.
  int num_samples;

  // Number of values over which the samples were collected.
  int64_t source_size;

  // Random number generator for generating 64-bit integers
  // TODO: Replace with mt19937_64 when upgrading boost
  ranlux64_3 rng;

  int64_t GetNext64(int64_t max) {
    uniform_int<int64_t> dist(0, max);
    return dist(rng);
  }
};

int SizeOfState(int max_samples) {
  return sizeof(ReservoirSampleState<DoubleVal>) + max_samples*sizeof(ReservoirSample<DoubleVal>);
}

void ReservoirSampleInit(FunctionContext* ctx, StringVal* dst) {
  // Allocation room for the state with no samples, due to a bug in Impala the
  // space for samples is allocated in the ReservoirSampleUpdate function
  int str_len = SizeOfState(0);
  dst->is_null = false;
  dst->ptr = ctx->Allocate(str_len);
  dst->len = str_len;
  memset(dst->ptr, 0, str_len);
  *reinterpret_cast<ReservoirSampleState<DoubleVal>*>(dst->ptr) = ReservoirSampleState<DoubleVal>();
}

void ReservoirSampleUpdate(FunctionContext* ctx, const DoubleVal& src, const IntVal& max_samples,
    StringVal* dst) {
  if (src.is_null) return;
  DCHECK(!dst->is_null);
  // This error checking is not as precise, but only happens during debug builds anyway
  DCHECK_GE(dst->len, SizeOfState(0));

  // Reallocate the sapce for samples if there isn't enough space. This will do bad things(tm)
  // if the second parameter to our function isn't a constant but checking for constant values
  // isn't working in Impala 2.2
  if (dst->len < SizeOfState(max_samples.val)) {
    dst->ptr = ctx->Reallocate(dst->ptr, SizeOfState(max_samples.val));
    dst->len = SizeOfState(max_samples.val);
  }

  ReservoirSampleState<DoubleVal>* state = reinterpret_cast<ReservoirSampleState<DoubleVal>*>(dst->ptr);
  // The samples are stored at the end of the buffer
  // This differs from the original implementation
  // where the samples were a statically allocated array
  state->samples = reinterpret_cast<ReservoirSample<DoubleVal>*>(dst->ptr + sizeof(ReservoirSampleState<DoubleVal>));

  if (state->num_samples < max_samples.val) {
    state->samples[state->num_samples++] = ReservoirSample<DoubleVal>(src);
  } else {
    int64_t r = state->GetNext64(state->source_size);
    if (r < max_samples.val) state->samples[r] = ReservoirSample<DoubleVal>(src);
  }
  ++state->source_size;

  // If the number of max samples has gone up, update the state.
  // We don't handle the max samples going down.
  // The max samples *should* be a constant, but this is a hedge against that
  if (max_samples.val > state->max_samples) {
    state->max_samples = max_samples.val;
  }
}

const StringVal ReservoirSampleSerialize(FunctionContext* ctx,
    const StringVal& src) {
  if (src.is_null) return src;
  StringVal result(ctx, src.len);
  memcpy(result.ptr, src.ptr, src.len);
  ctx->Free(src.ptr);

  ReservoirSampleState<DoubleVal>* state = reinterpret_cast<ReservoirSampleState<DoubleVal>*>(result.ptr);
  // The samples are stored at the end of the buffer
  state->samples = reinterpret_cast<ReservoirSample<DoubleVal>*>(result.ptr + sizeof(ReservoirSampleState<DoubleVal>));

  // Assign keys to the samples that haven't been set (i.e. if serializing after
  // Update()). In weighted reservoir sampling the keys are typically assigned as the
  // sources are being sampled, but this requires maintaining the samples in sorted order
  // (by key) and it accomplishes the same thing at this point because all data points
  // coming into Update() get the same weight. When the samples are later merged, they do
  // have different weights (set here) that are proportional to the source_size, i.e.
  // samples selected from a larger stream are more likely to end up in the final sample
  // set. In order to avoid the extra overhead in Update(), we approximate the keys by
  // picking random numbers in the range [(SOURCE_SIZE - SAMPLE_SIZE)/(SOURCE_SIZE), 1].
  // This weights the keys by SOURCE_SIZE and implies that the samples picked had the
  // highest keys, because values not sampled would have keys between 0 and
  // (SOURCE_SIZE - SAMPLE_SIZE)/(SOURCE_SIZE).
  for (int i = 0; i < state->num_samples; ++i) {
    if (state->samples[i].key >= 0) continue;
    int r = rand() % state->num_samples;
    state->samples[i].key = ((double) state->source_size - r) / state->source_size;
  }
  return result;
}

template <typename T>
bool SampleValLess(const ReservoirSample<T>& i, const ReservoirSample<T>& j) {
  return i.val.val < j.val.val;
}

template <>
bool SampleValLess(const ReservoirSample<StringVal>& i,
    const ReservoirSample<StringVal>& j) {
  int n = min(i.len, j.len);
  int result = memcmp(&i.val[0], &j.val[0], n);
  if (result == 0) return i.len < j.len;
  return result < 0;
}

template <>
bool SampleValLess(const ReservoirSample<DecimalVal>& i,
    const ReservoirSample<DecimalVal>& j) {
  return i.val.val16 < j.val.val16;
}

template <>
bool SampleValLess(const ReservoirSample<TimestampVal>& i,
    const ReservoirSample<TimestampVal>& j) {
  if (i.val.date == j.val.date) return i.val.time_of_day < j.val.time_of_day;
  else return i.val.date < j.val.date;
}

template <typename T>
bool SampleKeyGreater(const ReservoirSample<T>& i, const ReservoirSample<T>& j) {
  return i.key > j.key;
}

void ReservoirSampleMerge(FunctionContext* ctx,
    const StringVal& src_val, StringVal* dst_val) {
  if (src_val.is_null) return;
  DCHECK(!dst_val->is_null);
  DCHECK(!src_val.is_null);

  // This error checking is not as precise, but only happens during debug builds anyway
  DCHECK_GE(src_val.len, SizeOfState(0));
  DCHECK_GE(dst_val->len, SizeOfState(0));

  ReservoirSampleState<DoubleVal>* src = reinterpret_cast<ReservoirSampleState<DoubleVal>*>(src_val.ptr);
  ReservoirSampleState<DoubleVal>* dst = reinterpret_cast<ReservoirSampleState<DoubleVal>*>(dst_val->ptr);

  // Set max_samples based on the MAX(src->max_samples, dst->max_samples)
  int max_samples = src->max_samples;
  if (dst->max_samples > max_samples) {
    max_samples = dst->max_samples;
  }

  // Only the destination is modifiable, so if it doesn't have enough space for
  // max_samples, reallocate it
  if (dst_val->len < SizeOfState(max_samples)) {
    dst_val->ptr = ctx->Reallocate(dst_val->ptr, SizeOfState(max_samples));
    dst_val->len = SizeOfState(max_samples);
    dst = reinterpret_cast<ReservoirSampleState<DoubleVal>*>(dst_val->ptr);
    dst->max_samples = max_samples;
  }

  // The samples are stored at the end of the buffer
  src->samples = reinterpret_cast<ReservoirSample<DoubleVal>*>(src_val.ptr + sizeof(ReservoirSampleState<DoubleVal>));
  dst->samples = reinterpret_cast<ReservoirSample<DoubleVal>*>(dst_val->ptr + sizeof(ReservoirSampleState<DoubleVal>));

  int src_idx = 0;
  int src_max = src->num_samples;

  // First, fill up the dst samples if they don't already exist. The samples are now
  // ordered as a min-heap on the key.
  while (dst->num_samples < max_samples && src_idx < src_max) {
    DCHECK_GE(src->samples[src_idx].key, 0);
    dst->samples[dst->num_samples++] = src->samples[src_idx++];
    push_heap(dst->samples, dst->samples + dst->num_samples, SampleKeyGreater<DoubleVal>);
  }
  // Then for every sample from source, take the sample if the key is greater than
  // the minimum key in the min-heap.
  while (src_idx < src_max) {
    DCHECK_GE(src->samples[src_idx].key, 0);
    if (src->samples[src_idx].key > dst->samples[0].key) {
      pop_heap(dst->samples, dst->samples + max_samples, SampleKeyGreater<DoubleVal>);
      dst->samples[max_samples - 1] = src->samples[src_idx];
      push_heap(dst->samples, dst->samples + max_samples, SampleKeyGreater<DoubleVal>);
    }
    ++src_idx;
  }
  dst->source_size += src->source_size;
}

StringVal AppxMedianFinalize(FunctionContext* ctx,
    const StringVal& src_val) {

  DCHECK(!src_val.is_null);
  // This error checking is not as precise, but only happens during debug builds anyway
  DCHECK_GE(src_val.len, SizeOfState(0));

  ReservoirSampleState<DoubleVal>* src = reinterpret_cast<ReservoirSampleState<DoubleVal>*>(src_val.ptr);
  // The samples are stored at the end of the buffer
  src->samples = reinterpret_cast<ReservoirSample<DoubleVal>*>(src_val.ptr + sizeof(ReservoirSampleState<DoubleVal>));

  if (src->num_samples == 0) {
    ctx->Free(src_val.ptr);
    return StringVal::null();
  }
  sort(src->samples, src->samples + src->num_samples, SampleValLess<DoubleVal>);

  DoubleVal median = src->samples[src->num_samples / 2].GetValue(ctx);
  ctx->Free(src_val.ptr);
  return ToStringVal(ctx, median);
}
