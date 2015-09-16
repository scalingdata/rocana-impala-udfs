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


#ifndef MEDIAN_H
#define MEDIAN_H

#include <impala_udf/udf.h>

using namespace impala_udf;

// Utility function for serialization to StringVal from Cloudera sample code
template <typename T>
StringVal ToStringVal(FunctionContext* context, const T& val);

// Approximate median based on reservoir sampling. Adapted from Impala's
// built-in APPX_MEDIAN function. This version takes a second parameter
// that is the maximum number of samples to cap memory usage.

// Usage: > create aggregate function appx_median_bounded(double, int) returns string
//          location '/user/impala/udfs/librocana-udfs.so'
//          init_fn='ReservoirSampleInit'
//          update_fn='ReservoirSampleUpdate'
//          merge_fn='ReservoirSampleMerge'
//          serialize_fn='ReservoirSampleSerialize'
//          finalize_fn='AppxMedianFinalize';
//        > select cast(appx_median_bounded(col, 100) as dobule) from tbl;
//
//

void ReservoirSampleInit(FunctionContext*, StringVal* slot);

void ReservoirSampleUpdate(FunctionContext*, const DoubleVal& src, const IntVal& max_samples, StringVal* dst);

void ReservoirSampleMerge(FunctionContext*, const StringVal& src, StringVal* dst);

const StringVal ReservoirSampleSerialize(FunctionContext*, const StringVal& src);

StringVal AppxMedianFinalize(FunctionContext*, const StringVal& src);


// Logging/debugging utils from RE2.
// Copyright 2009 The RE2 Authors.  All Rights Reserved.

// Debug-only checking.
#define DCHECK(condition) assert(condition)
#define DCHECK_EQ(val1, val2) assert((val1) == (val2))
#define DCHECK_NE(val1, val2) assert((val1) != (val2))
#define DCHECK_LE(val1, val2) assert((val1) <= (val2))
#define DCHECK_LT(val1, val2) assert((val1) < (val2))
#define DCHECK_GE(val1, val2) assert((val1) >= (val2))
#define DCHECK_GT(val1, val2) assert((val1) > (val2))

#endif
