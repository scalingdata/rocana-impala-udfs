# rocana-impala-udfs
Various Impala UDFs and UDAFs

## Building, Testing, and Installing

1. Use CMake to generate build files
 
 ```console
 $ cmake .
 ```
2. Build a shared object file
 
 ```console
 $ make
 ```
3. Run unit tests
 
 ```console
 $ build/rocana-udfs-test
 ```
4. Copy shared object file to HDFS
 
 ```console
 $ sudo -u hdfs hdfs dfs -mkdir /user/impala/udfs
 $ sudo -u hdfs hdfs dfs -put librocana-udfs.so /user/impala/udfs
 $ sudo -u hdfs hdfs dfs -chown -R impala:impala /user/impala/udfs
 ```

## Functions
### Bounded Median

A port of Impala's APPX_MEDIAN function that accepts a parameter that controls the maximum number of samples.

#### Usage
These steps assume you've installed the UDFs shared object file as described in Building and Installing above.

1. Create the function
 
 ```sql
 CREATE AGGREGATE FUNCTION appx_median_bounded(double, int) returns string
    location '/user/impala/udfs/librocana-udfs.so'
    init_fn='ReservoirSampleInit'
    update_fn='ReservoirSampleUpdate'
    merge_fn='ReservoirSampleMerge'
    serialize_fn='ReservoirSampleSerialize'
    finalize_fn='AppxMedianFinalize';
 ```
2. Create a test table

 ```sql
 CREATE TABLE numbers (x DOUBLE);
 ```
3. Insert some simple data
 
 ```sql
 INSERT INTO numbers VALUES (1.1), (2.2), (3.3), (4.4), (5.5);
 ```
4. Calculate median of the data
 
 ```sql
 SELECT CAST(appx_median_bounded(x, 5) AS DOUBLE) FROM numbers;
 ```
