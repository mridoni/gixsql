using gix_ide_tests;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.IO;

namespace gixsql_tests
{
    [TestClass]
    [HostPlatform("x64")]
    [TestCategory("Add support for VARYING groups (#38)")]
    public class TSQL022 : GixSqlTestBase
    {
        [TestInitialize]
        public new void Begin()
        {
            base.Begin();

            Environment.SetEnvironmentVariable("GIXSQL_DEBUG_LOG_ON", "1");
            Environment.SetEnvironmentVariable("GIXSQL_DEBUG_LOG", Path.Combine(TestTempDir, "gisql-debug.log"));
            Environment.SetEnvironmentVariable("GIXSQL_ERR_LOG", Path.Combine(TestTempDir, "gisql-error.log"));
        }


        // Standard
        [TestMethod]
        [CobolSource("TSQL022A.cbl")]
        [GixSqlDataSource("pgsql", 1)]
        public void TSQL022A_MSVC_pgsql_x64_exe_1()
        {
            compile(CompilerType.MSVC, "release", "x64", "exe", false, false);

            check_file_contains(LastPreprocessedFile, new string[]
            {
                "GIXSQL*    01 VBFLD SQL TYPE IS VARBINARY(100).",
                "49 VBFLD-LEN PIC 9(4) BINARY.",
                "49 VBFLD-ARR PIC X(100).",

                "GIXSQL*    01 VCFLD PIC X(100) VARYING.",
                "49 VCFLD-LEN PIC 9(4) BINARY.",
                "49 VCFLD-ARR PIC X(100)."
            });
        }

        //Custom suffixes
        [TestMethod]
        [CobolSource("TSQL022A.cbl", "EMPREC.cpy")]
        [GixSqlDataSource("pgsql", 1)]
        public void TSQL022A_MSVC_pgsql_x64_exe_2()
        {
            compile(CompilerType.MSVC, "release", "x64", "exe", false, false, "--varying=LLLL,AAAA");

            check_file_contains(LastPreprocessedFile, new string[]
            {
                "GIXSQL*    01 VBFLD SQL TYPE IS VARBINARY(100).",
                "49 VBFLD-LLLL PIC 9(4) BINARY.",
                "49 VBFLD-AAAA PIC X(100).",

                "GIXSQL*    01 VCFLD PIC X(100) VARYING.",
                "49 VCFLD-LLLL PIC 9(4) BINARY.",
                "49 VCFLD-AAAA PIC X(100)."
            });
        }
    }
}
