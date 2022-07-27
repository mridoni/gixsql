using gix_ide_tests;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Data.Common;
using System.IO;
using System.Linq;

namespace gixsql_tests
{
    [TestClass]
    [HostPlatform("x64")]
    public class TSQL030 : GixSqlTestBase
    {
        [TestInitialize]
        public new void Begin()
        {
            base.Begin();
        }

  
        [TestMethod]
        [CobolSource("TSQL030A.cbl")]
        [GixSqlDataSource("pgsql", 1)]
        [TestCategory("Show-stopper bug in pgsql_prepare (#91)")]
        //[Ignore]
        public void TSQL030A_MSVC_pgsql_x64_exe()
        {

            compile(CompilerType.MSVC, "release", "x64", "exe");

            string datasrc = build_data_source_string(false, true, true);
            Environment.SetEnvironmentVariable("DATASRC", datasrc);
            Environment.SetEnvironmentVariable("DATASRC_USR", get_datasource_usr() + "." + get_datasource_pwd());

            string payload = Utils.RandomString(32);
            Environment.SetEnvironmentVariable("PAYLOAD", payload);

            run(CompilerType.MSVC, "release", "x64", "exe", "", false, new string[] {
                "SET APPLICATION_NAME TO \"Identifier1 Identifier2 Identifier3\"\0"
            });
        }

    }
}
