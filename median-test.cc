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

#include <iostream>
#include <math.h>

#include <impala_udf/uda-test-harness.h>
#include <impala_udf/udf-test-harness.h>
#include "median.h"

using namespace impala;
using namespace impala_udf;
using namespace std;

// For algorithms that work on floating point values, the results might not match
// exactly due to floating point inprecision. The test harness allows passing a
// custom equality comparator. Here's an example of one that can tolerate some small
// error.
// This is particularly true  for distributed execution since the order the values
// are processed is variable.
bool FuzzyCompare(const DoubleVal& x, const DoubleVal& y) {
  if (x.is_null && y.is_null) return true;
  if (x.is_null || y.is_null) return false;
  return fabs(x.val - y.val) < 0.00001;
}

// Reimplementation of FuzzyCompare that parses doubles encoded as StringVals.
// TODO: This can be removed when separate intermediate types are supported in Impala 2.0
bool FuzzyCompareStrings(const StringVal& x, const StringVal& y) {
  if (x.is_null && y.is_null) return true;
  if (x.is_null || y.is_null) return false;
  // Note that atof expects null-terminated strings, which is not guaranteed by
  // StringVals. However, since our UDAs serialize double to StringVals via stringstream,
  // we know the serialized StringVals will be null-terminated in this case.
  double x_val = atof(string(reinterpret_cast<char*>(x.ptr), x.len).c_str());
  double y_val = atof(string(reinterpret_cast<char*>(y.ptr), y.len).c_str());
  return fabs(x_val - y_val) < 0.00001;
}

bool TestMedian() {
  // Setup the test UDAs.
  UdaTestHarness2<StringVal, StringVal, DoubleVal, IntVal> median(
      ReservoirSampleInit, ReservoirSampleUpdate, ReservoirSampleMerge, ReservoirSampleSerialize, AppxMedianFinalize);
  median.SetResultComparator(FuzzyCompareStrings);


  // Test empty input
  vector<DoubleVal> vals;
  vector<IntVal> samples;
  if (!median.Execute(vals, samples, StringVal::null())) {
    cerr << "Median: " << median.GetErrorMsg() << endl;
    return false;
  }

  // Initialize the test values.
  for (int i = 0; i < 1001; ++i) {
    vals.push_back(DoubleVal(i));
    samples.push_back(IntVal(1001));
  }

  double expected_median = 500;
  stringstream expected_median_ss;
  expected_median_ss << expected_median;
  string expected_median_str = expected_median_ss.str();
  StringVal expected_median_sv(expected_median_str.c_str());

  // Run the tests
  if (!median.Execute(vals, samples, expected_median_sv)) {
    cerr << "Median: " << median.GetErrorMsg() << endl;
    return false;
  }

  return true;
}

int main(int argc, char** argv) {
  bool passed = true;
  passed &= TestMedian();
  cerr << (passed ? "Tests passed." : "Tests failed.") << endl;
  return 0;
}
