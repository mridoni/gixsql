## The GixSQL test suite

GixSQL includes a self-contained test runner with an extendable test suite. The test runner is written in C# and runs under .Net 6.0, both under Windows and Linux. Each test case in the test suite is written to validate the correct implementation of a particular feature, at a syntactic or functional level.

### Components of the test suite

The test suite comprises several elements:

- The test runner (named by default `gixsql-tests-nunit.dll`)
- A "local" configuration file, that includes information about the database driver, environment variables and some general parameters
- A "test suite" configuration file that includes information about thest cases, like source files, environment variables, parameters

### Running the test suite

### Preparing your test environment

The test suite can run the test cases against one or more of the supplied database drivers (MySQL, ODBC, Oracle, PostgreSQL, SQLite). In order to perform the tests, you will have to create database and credentials to be used during testing. **DO NOT USE** existing database and, possibily, do not use servers that contain important or production data: the test runner creates and drops tables, and executes scripts. It is unlikely these might affect pre-existing data, but just in case...

You will need to create at least two databases (or schemas, depending on the nomenclature used by the DBMSs you are testing against) and a set of credentials with full access to the database/schema, including table creation. Most of the tests require only one data source (schema/database).

Note that on Oracle it is probably easier two create two different users with a corresponding schema. On the other DBMSs you can create different schemas/databases and give access to a single user. For SQLite there is no need to create anything, the DB (with no authentication) will be created during the tests, you will just need to supply a filename (see below). In PostgreSQL you can use two schemas in the same databases and add the `default_schema` parameter to the connection string, but is just faster to create two separate databases.

If you want to keep and examine the test logs (recommended when debugging a particular test case, create a directory that will be used in the local configuration (see below).

You obviously need to build and install GixSQL, or use one of the binary packages. Just take note of the location where GixSQL is installed. When using the binary packages, this is by default `/usr` on Linux and `C:\Program Files\GixSQL` on Windows.

You can also build and install a version of GixSQL in a separate directory (e.g. `/opt/gixsql`) just for testing.

#### Building the test runner

Both for Windows and Linux install the .Net 6.0 SDK from [here](https://dotnet.microsoft.com/en-us/download/dotnet/6.0). Depending on your preferences, on Linux you might install the distribution-supplied packages, but beware that these might not be up to date.

Note: for the sake of simplicity all paths are written here in Linux style, with a `/` separator, feel free to use backslashes where appropriate or needed.

After the .Net 6.0 SDK has been installed, position yourself in the root of the GixSQL source directory and build the test runner:

    dotnet build gixsql-tests-nunit/gixsql-tests-nunit.csproj


If all goes well, there should be no errors, and the test runner should be available under `<gixsql source>/bin/Debug/net6.0` as `gixsql-tests-nunit.dll`.

### Local configuration

You can start from one of the supplied configuration files (`gixsql_test_local_linux.xml` or `gixsql_test_local_linux.xml`). This is an XML file that contains information about the local environment where the tests are going to be run. Copy it with a new name and open it in an editor. 



	<global>
		<gixsql-install-base>/home/marchetto/gixsql/dist</gixsql-install-base>
		<keep-temps>1</keep-temps>
		<verbose>0</verbose>
		<test-filter></test-filter>
		<dbtype-filter></dbtype-filter>
		<mem-check>valgrind --log-file=valgrind-${testid}.txt --leak-check=full</mem-check>
		<temp-dir>/tmp/gixsql-test</temp-dir>
		<environment>
			<variable key="GIXSQL_FIXUP_PARAMS" value="on" />
		</environment>
	</global>

Here you will have to check and edit some parameters for the test runner:

- **gixsql-install-base**:this is the location where GixSQL is installed
- **keep-temps** if set to "1", temporary test files will be kept around (this is recommended)
- **verbose**: generate (sometimes a lot) more output while runninf the tests. Normally leave it to "0"
- **dbtype-filter**: a comma-separated list of DB "types" for which the tests will be run. For example, if you are only intested in running tests for Postgresql and MySQL, use `<dbtype-filter>pgsql,mysql</dbtype-filter>`
- **mem-check**: if set, the tests will be run under a memory checker (usually valgrind on Linux and DrMemory on Windows) using the command line supplied. here you can use several macros:

    - ${testid} : the test ID
    - ${dbtype} : the current DB type
    - ${arch} : the current architecture
    - ${date} : the current date (in ISO format)
    - ${time} : the current time (in ISO format)
    
   If you don't want to use a memory checker just leave this field blank
   
- **temp-dir**: the directory where the test outputs and logs will be stored. If left blank, a temporary directory will be created at runtime.
- **environment** : in this section you can map the environment variables that will be possibly needed for your tests. The `GIXSQL_FIXUP_PARAMS` is needed for some of the included tests, so it is advisable to leave it there.

(to be continued)

