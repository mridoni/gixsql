## The GixSQL test suite

GixSQL includes a self-contained test runner with an extendable test suite. The test runner is written in C# and runs under .Net 6.0, both under Windows and Linux. Each test case in the test suite is written to validate the correct implementation of a particular feature, at a syntactic or functional level.

### Components of the test suite

The test suite comprises several elements:

- The test runner (named by default `gixsql-tests-nunit.dll`)
- A "local" configuration file, that includes information about the database driver, environment variables and some general parameters
- A "test suite" configuration file that includes information about test cases, like source files, environment variables, parameters

### Running the test suite

### Preparing your test environment

The test suite can run the test cases against one or more of the supplied database drivers (MySQL, ODBC, Oracle, PostgreSQL, SQLite). In order to perform the tests, you will have to create database and credentials to be used during testing. **DO NOT USE** existing database and, possibly, do not use servers that contain important or production data: the test runner creates and drops tables, and executes scripts. It is unlikely these might affect pre-existing data, but just in case...

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


#### The "global" section

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
- **verbose**: generate (sometimes a lot) more output while running the tests. Normally leave it to "0"
- **dbtype-filter**: a comma-separated list of DB "types" for which the tests will be run. For example, if you are only intested in running tests for PostgreSQL and MySQL, use `<dbtype-filter>pgsql,mysql</dbtype-filter>`. The identifiers (also used in all the configuration elements where a "DB driver type" is needed) are `mysql`, `pgsql`, `odbc`, `oracle`, `sqlite`
- **mem-check**: if set, the tests will be run under a memory checker (usually valgrind on Linux and DrMemory on Windows) using the command line supplied. here you can use several macros:

    - ${testid} : the test ID
    - ${dbtype} : the current DB type
    - ${arch} : the current architecture
    - ${date} : the current date (in ISO format)
    - ${time} : the current time (in ISO format)
    
   If you don't want to use a memory checker just leave this field blank
   
- **temp-dir**: the directory where the test outputs and logs will be stored. If left blank, a temporary directory will be created at runtime.
- **environment** : in this section you can map the environment variables that will be possibly needed for your tests. The `GIXSQL_FIXUP_PARAMS` is needed for some of the included tests, so it is advisable to leave it there.


#### The "architectures" section

The test runner can perform tests for the different architectures supported by the machine where you are running the tests. In practice this means you can run, if so desired, also x86 tests on an x64 target: you cannot obviously do it the other way around. This is not usually needed, and tests for your "main" architecture should be indicative. If you are running on x64, it is probably fine to leave the x86 architecture commented (or the other way around).

	<architectures>
		<architecture id="x64" />
		<!--<architecture id="x86" />-->
	</architectures>


#### The "compiler-types" section

This mainly affects Windows, where GnuCOBOL can target either the Microsoft Visual C linker and runtime, or the one provided by MSYS2 (i.e. MinGW32/64). You can simultaneously test both compiler versions (you will need to have them installed in separate directories,obviously). Otherwise, pick one and comment out the one you are not using. 

	<compiler-types>
		<compiler-type id="msvc" />
		<!--<compiler-type id="gcc" />-->
	</compiler-types>


#### The "compilers" section

This section lists in detail the different instances of GnuCOBOL installed in your test environment and available for testing.


	<compilers>
		<compiler type="msvc" architecture="x64" id="gnucobol-3.1.2-windows-msvc-all">
			<bin_dir_path>%localappdata%\Gix\compiler-pkgs\gnucobol-3.1.2-windows-msvc-all\bin_x64</bin_dir_path>
			<lib_dir_path>%localappdata%\Gix\compiler-pkgs\gnucobol-3.1.2-windows-msvc-all\lib_x64</lib_dir_path>
			<config_dir_path>%localappdata%\Gix\compiler-pkgs\gnucobol-3.1.2-windows-msvc-all\config</config_dir_path>
			<environment>
			</environment>
		</compiler>		
		<compiler type="msvc" architecture="x86" id="gnucobol-3.1.2-windows-msvc-all">
			<bin_dir_path>%localappdata%\Gix\compiler-pkgs\gnucobol-3.1.2-windows-msvc-all\bin_x86</bin_dir_path>
			<lib_dir_path>%localappdata%\Gix\compiler-pkgs\gnucobol-3.1.2-windows-msvc-all\lib_x86</lib_dir_path>
			<config_dir_path>%localappdata%\Gix\compiler-pkgs\gnucobol-3.1.2-windows-msvc-all\config</config_dir_path>
			<environment>
			</environment>
		</compiler>
		<compiler type="gcc" architecture="x64" id="gnucobol-3.1.2-windows-mingw-x64">
			<bin_dir_path>%localappdata%\Gix\compiler-pkgs\gnucobol-3.1.2-windows-mingw-x64\bin</bin_dir_path>
			<lib_dir_path>%localappdata%\Gix\compiler-pkgs\gnucobol-3.1.2-windows-mingw-x64\lib</lib_dir_path>
			<config_dir_path>%localappdata%\Gix\compiler-pkgs\gnucobol-3.1.2-windows-mingw-x64\config</config_dir_path>
			<environment>
			</environment>
		</compiler>
		<compiler type="gcc" architecture="x86" id="gnucobol-3.1.2-windows-mingw-x86">
			<bin_dir_path>%localappdata%\Gix\compiler-pkgs\gnucobol-3.1.2-windows-mingw-x86\bin</bin_dir_path>
			<lib_dir_path>%localappdata%\Gix\compiler-pkgs\gnucobol-3.1.2-windows-mingw-x86\lib</lib_dir_path>
			<config_dir_path>%localappdata%\Gix\compiler-pkgs\gnucobol-3.1.2-windows-mingw-x86\config</config_dir_path>
			<environment>
			</environment>
		</compiler>
	</compilers>
	


In this example we have four compilers: two are MSVC-enabled (see above), the others are MinGW32/64-enabled, in 32 and 64 bit versions. Note that these are the "available" compilers, the one(s) that will be actually used will be determined by the combination of the parameters specified in the `architectures` and `compiler-types` sections: if, for instance, you only have one architecture and one compiler type, only one compiler definition is needed.

As you can see, in the "path" fields, you can use environment variables, that will be taken from the environment under which the test runner is running.

This is the common case on Linux, where the compiler section tends to be much simpler, especially if you are using a modern distribution and a distribution-supplied version of GnuCOBOL:

	<compilers>
		<compiler type="gcc" architecture="x64" id="gnucobol-3.1.2-linux-gcc-x64">
			<bin_dir_path>/usr/bin</bin_dir_path>
			<lib_dir_path>/usr/lib</lib_dir_path>
			<config_dir_path>/etc/gnucobol</config_dir_path>
			<environment>
			</environment>
		</compiler>
	</compilers>
	
	

#### The "data-source-clients" section

This section lists the parameters that will be used to configure the data source clients used during testing. These are NOT the data sources themselves (i.e. connection parameters for a given database/schema) but the information about client libraries that will be used to make the connection (e.g. `libpq` on PostgreSQL or `libmariadb`for MySQL/MariaDB).

We need this because the location of the client libraries can be different on each test environment and moreover, while the binary distribution of GixSQL already includes the client libraries themselves, you might want to target (or test) a different version or decide not to use the included version of the client libraries.

Again: this is mostly significant on Windows, where usually there is a much more ample choice on where and how to install the DBMS and its client libraries. On Linux, unless you are performing a custom install of your DBMS (and/or of its client libraries) you won't have to worry about this. The (partial) configuration reported here is typical of a Widows development environment, where the client libraries are supplied through `vcpkg`.

	<data-source-clients>
		<data-source-client type="pgsql" architecture="x64">
			<environment>
				<variable key="PATH" value="C:\vcpkg\installed\x64-windows\bin;C:\vcpkg\installed\x64-mingw-dynamic\bin" />
			</environment>
			<provider value="Npgsql" />
		</data-source-client>

		<data-source-client type="mysql" architecture="x64">
			<environment>
				<variable key="PATH" value="C:\vcpkg\installed\x64-windows\bin;C:\vcpkg\installed\x64-mingw-dynamic\bin" />
			</environment>
			<provider value="MySql.Data.MySqlClient" />
			<additional-preprocess-params value="-z a" />
		</data-source-client>

		<data-source-client type="oracle" architecture="x64">
			<environment>
				<variable key="PATH" value="D:\oracle\instantclient_21_6_x64" />
			</environment>
			<provider value="Oracle.ManagedDataAccess.Client" />
			<additional-preprocess-params value="-z c" />
		</data-source-client>

		<data-source-client type="sqlite" architecture="x64">
			<environment>
			</environment>
			<provider value="System.Data.SQLite" />
			<additional-preprocess-params value="-z c" />
		</data-source-client>

		<data-source-client type="odbc" architecture="x64">
			<environment>
			</environment>
			<provider value="System.Data.Odbc" />
			<additional-preprocess-params value="-z a" />
		</data-source-client>

	</data-source-clients>

As you can see, you can also indicate some driver-specific environment variables that will be set when using a specific driver and a set of custom preprocessor parameters that will be used when preprocessing a COBOL source file to be tested with a specific driver. This is needed because in some cases we need to customize the output (e.g. parameter format) for a specific database.

This is, again, normally simpler on Linux, since the client libraries are normally installed with your package manager and do not need custom configurations to be used.

	<data-source-clients>
		<data-source-client type="pgsql" architecture="x64">
			<environment>
			</environment>
			<provider value="Npgsql" />
		</data-source-client>

		<data-source-client type="mysql" architecture="x64">
			<environment>
			</environment>
			<provider value="MySql.Data.MySqlClient" />
			<additional-preprocess-params value="-z a" />
		</data-source-client>

		<data-source-client type="oracle" architecture="x64">
			<environment>
			</environment>
			<provider value="Oracle.ManagedDataAccess.Client" />
			<additional-preprocess-params value="-z c" />
		</data-source-client>

		<data-source-client type="sqlite" architecture="x64">
			<environment>
			</environment>
			<provider value="System.Data.SQLite" />
			<additional-preprocess-params value="-z c" />
		</data-source-client>

		<data-source-client type="odbc" architecture="x64">
			<environment>
			</environment>
			<provider value="System.Data.Odbc" />
			<additional-preprocess-params value="-z a" />
		</data-source-client>
	</data-source-clients>


In all cases the `provider` parameter identifies the .Net driver for a given database. This is needed because the test runner (remember: that is written in C#/.Net) performs some actions on the DB before running the tests (e.g. cleanup, setup the schema, prepare table the data to be used while testing) so we need to be sure that the DB is not only accessible from the COBOL-side, but also by the test runner *before* running the test. There is normally no need to change this parameter, just leave it as you found it.

#### The "data-sources" section

This is where you define the "data sources", and enter the connection parameters (i.e. hostname, credentials, etc.) for the databases/schemas you have created before.

	<data-sources>
		<data-source type="pgsql" index="1">
			<hostname>192.168.56.1</hostname>
			<port>5432</port>
			<dbname>testdb1</dbname>
			<username>test</username>
			<password>test</password>
			<options>native_cursors=off</options>
		</data-source>
		<data-source type="pgsql" index="2">
			<hostname>192.168.56.1</hostname>
			<port>5432</port>
			<dbname>testdb2</dbname>
			<username>test</username>
			<password>test</password>
			<options>native_cursors=off</options>
		</data-source>
        
		<data-source type="mysql" index="1">
			<hostname>192.168.1.171</hostname>
			<port>3306</port>
			<dbname>testdb1</dbname>
			<username>test</username>
			<password>test</password>
			<options></options>
		</data-source>
		...
	</data-sources>
    

You only need to define the data sources for the DBMS type that you will test against. As said above, you will need at least two data sources for each DBMS type. If you just use one, but a small subset of the tests (the ones targeting multiple connection will surely fail).

The `options` field contains the additional settings that are optionally passed with a standard GixSQL connection string, to enable, disable or configure specific options in the drivers. If you are using the standard test suite, you should change the connection information but leave the `options` field unchanged, to ensure that the standard tests will run successfully.

(to be continued)

