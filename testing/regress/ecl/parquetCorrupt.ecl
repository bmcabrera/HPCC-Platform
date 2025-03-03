/*##############################################################################
    HPCC SYSTEMS software Copyright (C) 2024 HPCC Systems®.
    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at
       http://www.apache.org/licenses/LICENSE-2.0
    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
############################################################################## */

//class=parquet
//fail
//nothor
//noroxie

//This ECL code reads a potentially corrupt Parquet file, handling the case where it might be corrupt
//by outputting either the file contents or a single informative record if the file can't be read.

IMPORT Parquet;

RECORDDEF := RECORD
    UNSIGNED4 index;
    STRING name;
    STRING director;
END;

string basePath := '' : STORED('OriginalTextFilesOsPath');

filePath := basePath + '/download/corrupt.parquet';

CORRUPT_PARQUET := ParquetIO.Read(RECORDDEF, filePath);

OUTPUT(CORRUPT_PARQUET);
