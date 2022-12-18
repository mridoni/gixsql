//using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.IO;
using System.Xml;

namespace gixsql_tests
{

    public class CompilerConfig2
    {
        public string compiler_id { get; private set; }

        public string cobc_bin_dir_path { get; private set; }
        public string cobc_lib_dir_path { get; private set; }
        public string cobc_config_dir_path { get; private set; }

        public string gixsql_bin_path { get; private set; }
        public string gixsql_lib_path { get; private set; }
        public string gixsql_copy_path { get; private set; }
        public string gixsql_link_lib_dir_path { get; private set; }
        public string gixsql_link_lib_name { get; private set; }
        public string gixsql_link_lib_lname { get; private set; }

        public string gixpp_exe { get; private set; }
        public string cobc_exe { get; private set; }
        public string cobcrun_exe { get; private set; }
        public bool IsVsBased { get; private set; }

        public static CompilerConfig2 init(XmlElement xc)
        {
            bool isWindows = !File.Exists(@"/proc/sys/kernel/ostype");

            try
            {
                CompilerConfig2 cc = new CompilerConfig2();

                string compiler_type = xc.Attributes["type"].Value;
                string compiler_arch = xc.Attributes["architecture"].Value;
                string compiler_id = xc.Attributes["id"].Value;

                string gix_base_path = Environment.ExpandEnvironmentVariables(TestDataProvider.TestGixSqlInstallBase);

                cc.compiler_id = compiler_id;

                cc.IsVsBased = compiler_type == "msvc";

                cc.cobc_bin_dir_path = Environment.ExpandEnvironmentVariables(xc.SelectSingleNode("bin_dir_path")?.InnerText);
                if (!Directory.Exists(cc.cobc_bin_dir_path)) throw new Exception(cc.cobc_bin_dir_path);

                cc.cobc_lib_dir_path = Environment.ExpandEnvironmentVariables(xc.SelectSingleNode("lib_dir_path")?.InnerText);
                if (!Directory.Exists(cc.cobc_lib_dir_path)) throw new Exception(cc.cobc_lib_dir_path);

                cc.cobc_config_dir_path = Environment.ExpandEnvironmentVariables(xc.SelectSingleNode("config_dir_path")?.InnerText);
                if (!Directory.Exists(cc.cobc_config_dir_path)) throw new Exception(cc.cobc_config_dir_path);

                if (isWindows)
                    cc.gixsql_copy_path = Path.Combine(gix_base_path, "lib", "copy");
                else
                    cc.gixsql_copy_path = Path.Combine(gix_base_path, "share", "config", "copy");

                if (!Directory.Exists(cc.gixsql_copy_path)) throw new Exception(cc.gixsql_copy_path);
                if (!File.Exists(Path.Combine(cc.gixsql_copy_path, "SQLCA.cpy"))) throw new Exception();

                cc.gixsql_bin_path = Path.Combine(gix_base_path, "bin"); 
                
                cc.gixsql_lib_path = Path.Combine(gix_base_path, "lib");
                cc.gixsql_link_lib_dir_path = Path.Combine(cc.gixsql_lib_path, compiler_arch, compiler_type);
                cc.gixsql_link_lib_name = cc.IsVsBased ? "libgixsql.lib" : "libgixsql.a";

                if (!File.Exists(Path.Combine(cc.gixsql_link_lib_dir_path, cc.gixsql_link_lib_name))) throw new Exception(Path.Combine(cc.gixsql_link_lib_dir_path, cc.gixsql_link_lib_name));

                if (isWindows)
                {
                    cc.gixpp_exe = Path.Combine(cc.gixsql_bin_path, "gixpp.exe");
                    if (!File.Exists(cc.gixpp_exe)) throw new Exception(cc.gixpp_exe);

                    cc.cobc_exe = Path.Combine(cc.cobc_bin_dir_path, "cobc.exe");
                    if (!File.Exists(cc.cobc_exe)) throw new Exception(cc.cobc_exe);

                    cc.cobcrun_exe = Path.Combine(cc.cobc_bin_dir_path, "cobcrun.exe");
                    if (!File.Exists(cc.cobcrun_exe)) throw new Exception(cc.cobcrun_exe);

                    cc.gixsql_link_lib_lname = cc.IsVsBased ? "libgixsql" : "gixsql";
                }
                else
                {
                    cc.gixpp_exe = Path.Combine(cc.gixsql_bin_path, "gixpp");
                    if (!File.Exists(cc.gixpp_exe)) throw new Exception(cc.gixpp_exe);

                    cc.cobc_exe = Path.Combine(cc.cobc_bin_dir_path, "cobc");
                    if (!File.Exists(cc.cobc_exe)) throw new Exception(cc.cobc_exe);

                    cc.cobcrun_exe = Path.Combine(cc.cobc_bin_dir_path, "cobcrun");
                    if (!File.Exists(cc.cobcrun_exe)) throw new Exception(cc.cobcrun_exe);

                    cc.gixsql_link_lib_lname = "gixsql";
                }



                return cc;
            }
            catch (Exception ex)
            {
                Console.WriteLine(ex.Message + "\n" + ex.StackTrace);
                throw ex;
            }
        }
    }
}
