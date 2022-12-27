//using Microsoft.VisualStudio.TestTools.UnitTesting;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Data.Common;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Intrinsics.X86;
using System.Text;
using System.Threading.Tasks;
using System.Xml;

namespace gixsql_tests
{

    public class TestDataProvider : Attribute
    {
        public static string TestTempDir => test_temp_dir;
        public static string TestGixSqlInstallBase => test_install_base;
        public static bool TestKeepTemps => test_keep_temps;
        public static bool TestVerbose => test_verbose;
        public static int TestCount => test_count;
        public static string Shell => shell;
        public static string MemCheck => mem_check;
        public static List<string> TestFilterList => test_filter_list;
        public static List<string> TestDbTypeFilterList => db_filter_list;

        private static List<string> available_architectures = new List<string>();
        private static List<string> available_compiler_types = new List<string>();

        private static Dictionary<Tuple<string, string>, CompilerConfig2> available_compilers = new Dictionary<Tuple<string, string>, CompilerConfig2>();
        private static Dictionary<Tuple<string, string>, GixSqlTestDataSourceClientInfo> available_data_source_clients = new Dictionary<Tuple<string, string>, GixSqlTestDataSourceClientInfo>();
        private static Dictionary<Tuple<string, int>, GixSqlTestDataSourceInfo> available_data_sources = new Dictionary<Tuple<string, int>, GixSqlTestDataSourceInfo>();

        private static Dictionary<string, string> global_env = new Dictionary<string, string>();

        private static string shell = null;
        private static string test_temp_dir = null;
        private static bool test_keep_temps = false;
        private static string mem_check = null;
        private static string test_install_base = null;
        private static bool test_verbose = false;
        private static int test_count = 0;
        private static List<string> test_filter_list = new List<string>();
        private static List<string> db_filter_list = new List<string>();

        //private static string test_data;

        static TestDataProvider()
        {
            DbProviderFactories.RegisterFactory("MySql.Data.MySqlClient", MySqlConnector.MySqlConnectorFactory.Instance);
            DbProviderFactories.RegisterFactory("Npgsql", Npgsql.NpgsqlFactory.Instance);
            DbProviderFactories.RegisterFactory("System.Data.SQLite", System.Data.SQLite.EF6.SQLiteProviderFactory.Instance);
            DbProviderFactories.RegisterFactory("Oracle.ManagedDataAccess.Client", Oracle.ManagedDataAccess.Client.OracleClientFactory.Instance);

            // Currently unsupported. Bug?
            DbProviderFactories.RegisterFactory("System.Data.Odbc", System.Data.Odbc.OdbcFactory.Instance);
        }

        private static void ReadLocalConfiguration()
        {
            try
            {
                Console.WriteLine("Reading local configuration");
                string[] bool_values = { "0", "1", "on", "off" };

                // local config file
                string local_config = Environment.GetEnvironmentVariable("GIXTEST_LOCAL_CONFIG");
                if (String.IsNullOrWhiteSpace(local_config) || !File.Exists(local_config))
                    throw new Exception("Invalid value for GIXTEST_LOCAL_CONFIG: " + local_config);

                XmlDocument doc = new XmlDocument();
                doc.Load(local_config);

                // Install base (required)
                XmlElement xg = (XmlElement)doc.DocumentElement.SelectSingleNode("./global/gixsql-install-base");
                if (xg == null || !Directory.Exists(xg.InnerText))
                {
                    throw new Exception("Invalid \"gixsql-install-base\" in " + local_config);
                }
                test_install_base = xg.InnerText;
                Console.WriteLine("Install base: " + test_install_base);

                // Keep temps (optional)
                xg = (XmlElement)doc.DocumentElement.SelectSingleNode("./global/keep-temps");
                if (xg != null && !bool_values.Contains(xg.InnerText.ToLower()))
                {
                    throw new Exception("Invalid \"gix-keep-temps\" in " + local_config);
                }
                if (xg != null)
                    test_keep_temps = xg.InnerText.ToLower() == "1" || xg.InnerText.ToLower() == "on";

                // Test temporary directory (optional)
                xg = (XmlElement)doc.DocumentElement.SelectSingleNode("./global/temp-dir");
                if (xg != null && !Directory.Exists(xg.InnerText))
                {
                    throw new Exception("Invalid \"temp-dir\" in " + local_config);
                }
                if (xg != null)
                    test_temp_dir = xg.InnerText;

                // Verbose output (optional)
                xg = (XmlElement)doc.DocumentElement.SelectSingleNode("./global/verbose");
                if (xg != null && !bool_values.Contains(xg.InnerText.ToLower()))
                {
                    throw new Exception("Invalid \"verbose\" in " + local_config);
                }
                if (xg != null)
                    test_verbose = xg.InnerText.ToLower() == "1" || xg.InnerText.ToLower() == "on";

                // test filter
                xg = (XmlElement)doc.DocumentElement.SelectSingleNode("./global/test-filter");
                if (xg != null && !String.IsNullOrWhiteSpace(xg.InnerText)) {
                    foreach (var f in xg.InnerText.Split(new String[] { ",", ";" }, StringSplitOptions.RemoveEmptyEntries))  {
                        test_filter_list.Add(f);
                    }
                }

                xg = (XmlElement)doc.DocumentElement.SelectSingleNode("./global/mem-check");
                if (xg != null && !String.IsNullOrWhiteSpace(xg.InnerText))  {
                    mem_check = xg.InnerText.ToLower();
                }

                // db type filter
                xg = (XmlElement)doc.DocumentElement.SelectSingleNode("./global/dbtype-filter");
                if (xg != null && !String.IsNullOrWhiteSpace(xg.InnerText))
                {
                    foreach (var f in xg.InnerText.Split(new String[] { ",", ";" }, StringSplitOptions.RemoveEmptyEntries))
                    {
                        db_filter_list.Add(f);
                    }
                }

                foreach (var xn in doc.SelectNodes("/test-local-config/architectures/architecture"))
                {
                    XmlElement xe = (XmlElement)xn;
                    available_architectures.Add(xe.Attributes["id"].Value);
                }

                foreach (var xn in doc.SelectNodes("/test-local-config/compiler-types/compiler-type"))
                {
                    XmlElement xe = (XmlElement)xn;
                    available_compiler_types.Add(xe.Attributes["id"].Value);
                }

                foreach (var xn in doc.SelectNodes("/test-local-config/compilers/compiler"))
                {
                    XmlElement xe = (XmlElement)xn;
                    string compiler_type = xe.Attributes["type"].Value;
                    string compiler_arch = xe.Attributes["architecture"].Value;
                    string compiler_id = xe.Attributes["id"].Value;
                    if (!available_compiler_types.Contains(compiler_type))
                        continue;

                    CompilerConfig2 cc = CompilerConfig2.init(xe);
                    Tuple<string, string> k = new Tuple<string, string>(compiler_type, compiler_arch);
                    available_compilers.Add(k, cc);
                }

                foreach (var xn in doc.SelectNodes("/test-local-config/data-source-clients/data-source-client"))
                {
                    XmlElement xe = (XmlElement)xn;
                    Dictionary<string, string> env = new Dictionary<string, string>();
                    foreach (var xenv in xe.SelectNodes("environment/variable"))
                    {
                        env.Add(((XmlElement)xenv).Attributes["key"].Value, ((XmlElement)xenv).Attributes["value"].Value);
                    }
                    Tuple<string, string> k = new Tuple<string, string>(xe.Attributes["type"].Value, xe.Attributes["architecture"].Value);
                    GixSqlTestDataSourceClientInfo ci = new GixSqlTestDataSourceClientInfo();
                    ci.environment = env;
                    ci.provider = xe["provider"].Attributes["value"].Value;
                    XmlElement xapp = xe["additional-preprocess-params"];
                    if (xapp != null)
                    {
                        if (xapp.HasAttribute("value") && !String.IsNullOrWhiteSpace(xapp.Attributes["value"].Value))
                        {
                            ci.additional_preprocess_params = xapp.Attributes["value"].Value;
                        }
                    }
                    available_data_source_clients.Add(k, ci);
                }

                foreach (var xn in doc.SelectNodes("/test-local-config/data-sources/data-source"))
                {
                    XmlElement xe = (XmlElement)xn;

                    Tuple<string, int> k = new Tuple<string, int>(xe.Attributes["type"].Value, Int32.Parse(xe.Attributes["index"].Value));
                    GixSqlTestDataSourceInfo ds = new GixSqlTestDataSourceInfo();
                    ds.type = k.Item1;
                    ds.hostname = xe["hostname"].InnerText;
                    ds.port = xe["port"].InnerText;
                    ds.dbname = xe["dbname"].InnerText;
                    ds.username = xe["username"].InnerText;
                    ds.password = xe["password"].InnerText;
                    ds.options = xe["options"].InnerText;

                    available_data_sources.Add(k, ds);
                }

                foreach (var xn in doc.SelectNodes("/test-local-config/global/environment/variable"))
                {
                    XmlElement xe = (XmlElement)xn;
                    global_env[xe.Attributes["key"].Value] = xe.Attributes["value"].Value;
                }
                Console.WriteLine("Done reading local configuration");
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine(ex.Message);
                Console.Error.WriteLine(ex.StackTrace);
                throw;
            }

        }


        public static IEnumerable GetData()
        {
            try
            {
                ReadLocalConfiguration();

                List<TestCaseData> data = new List<TestCaseData>();

                XmlDocument doc = new XmlDocument();

                // if not initialized, we use the embedded test matrix
                string testmatrix_config = Environment.GetEnvironmentVariable("GIXTEST_TESTMATRIX_CONFIG");
                if (!String.IsNullOrWhiteSpace(testmatrix_config) && !File.Exists(testmatrix_config))
                    throw new Exception("Invalid value for GIXTEST_TESTMATRIX_CONFIG");

                if (!String.IsNullOrWhiteSpace(testmatrix_config))
                {
                    doc.Load(testmatrix_config);
                    Console.WriteLine("Using test matrix from file " + testmatrix_config);
                }
                else
                {
                    doc.LoadXml(Utils.GetResource("gixsql_test_data.xml"));
                    Console.WriteLine("Using embedded test matrix");
                }


                foreach (string arch in available_architectures)
                {
                    foreach (string ctype in available_compiler_types)
                    {
                        var ads = available_data_source_clients.Where(a => a.Key.Item2 == arch).ToList();
                        foreach (Tuple<string, string> type_arch in ads.Select(a => a.Key))
                        {
                            var ds_type = type_arch.Item1;
                            var ds_client = available_data_source_clients[type_arch];
                            foreach (var xn in doc.SelectNodes("//tests/test"))
                            {
                                XmlElement xe = (XmlElement)xn;
                                if (xe.HasAttribute("enabled") && xe.Attributes["enabled"].Value == "false")
                                    continue;

                                if (xe.HasAttribute("applies-to") && xe.Attributes["applies-to"].Value != "all" && !xe.Attributes["applies-to"].Value.Split(',').Contains(ds_type))
                                    continue;

                                if (xe.HasAttribute("not-applies-to") && xe.Attributes["not-applies-to"].Value.Split(',').Contains(ds_type))
                                    continue;

                                GixSqlTestData td = new GixSqlTestData();
                                td.Name = xe.Attributes["name"].Value;
                                td.Architecture = arch;
                                td.CompilerType = ctype;

                                foreach (var ev in ds_client.environment)
                                {
                                    td.AddToEnvironment(ev.Key, ev.Value);
                                }

                                XmlElement xds = (XmlElement)xe["data-sources"];
                                int ds_count = xds != null ? Int32.Parse(xds.Attributes["count"].Value) : 1;

                                var dss = available_data_sources.Where(a => a.Key.Item1 == ds_type).ToList();
                                if (dss.Count < ds_count)
                                {
                                    throw new Exception("Insufficient data sources");
                                }

                                for (int i = 1; i <= ds_count; i++)
                                {
                                    var ds = dss.First(a => a.Key.Item1 == ds_type && a.Key.Item2 == i).Value;
                                    var dds = (GixSqlTestDataSourceInfo)ds.Clone();
                                    var p = "data-source-options[@data-source-index=\"" + i.ToString() + "\"]";
                                    var xopt = xe.SelectSingleNode(p);
                                    if (xopt != null && xopt.Attributes["value"] != null)
                                        dds.options = xopt.Attributes["value"].Value;

                                    td.DataSources.Add(dds);
                                }

                                foreach (var kve in global_env)
                                {
                                    string k = kve.Key;
                                    string v = kve.Value;
                                    td.AddToEnvironment(k, v);
                                }

                                foreach (XmlElement xv in xe.SelectNodes("environment/variable"))
                                {
                                    string k = xv.Attributes["key"].Value;
                                    string v = xv.Attributes["value"].Value;
                                    td.AddToEnvironment(k, v);
                                }


                                td.Compile = xe["compile"] != null && xe["compile"].HasAttribute("value") ? Boolean.Parse(xe["compile"].Attributes["value"].InnerText) : true;
                                td.Run = xe["run"] != null && xe["run"].HasAttribute("value") ? Boolean.Parse(xe["run"].Attributes["value"].InnerText) : true;

                                td.ExpectedToFailPreProcess = xe["expected-to-fail"] != null && xe["expected-to-fail"].HasAttribute("preprocess") ? Boolean.Parse(xe["expected-to-fail"].Attributes["preprocess"].InnerText) : false;
                                td.ExpectedToFailCobc = xe["expected-to-fail"] != null && xe["expected-to-fail"].HasAttribute("compile") ? Boolean.Parse(xe["expected-to-fail"].Attributes["compile"].InnerText) : false;
                                td.ExpectedToFailRun = xe["expected-to-fail"] != null && xe["expected-to-fail"].HasAttribute("run") ? Boolean.Parse(xe["expected-to-fail"].Attributes["run"].InnerText) : false;

                                td.AdditionalPreProcessParams = xe["additional-preprocess-params"] != null && xe["additional-preprocess-params"].HasAttribute("value") ? xe["additional-preprocess-params"].Attributes["value"].InnerText : String.Empty;
                                td.AdditionalCompileParams = xe["additional-compile-params"] != null && xe["additional-compile-params"].HasAttribute("value") ? xe["additional-compile-params"].Attributes["value"].InnerText : String.Empty;

                                td.Description = xe.SelectSingleNode("description").InnerText;
                                td.IssueCoverage = xe.SelectSingleNode("issue-coverage").InnerText;

                                if (test_filter_list.Count > 0 && !test_filter_list.Contains(td.Name))
                                {
                                    if (TestVerbose)
                                        Console.WriteLine("Skipping: " + td.FullName);

                                    continue;
                                }

                                if (db_filter_list.Count > 0)
                                {
                                    if (td.DataSources.Count == 0)
                                    {
                                        if (TestVerbose)
                                            Console.WriteLine("Skipping: " + td.FullName);

                                        continue;
                                    }

                                    if (!db_filter_list.Contains(td.DataSources[0].type))
                                    {
                                        if (TestVerbose)
                                            Console.WriteLine("Skipping: " + td.FullName);

                                        continue;
                                    }
                                }

                                Dictionary<string, string> translated_env = new Dictionary<string, string>();
                                foreach (var ev in td.Environment)
                                {
                                    string k = ev.Key;
                                    string v = ev.Value;

                                    for (int i = 1; i <= td.DataSources.Count; i++)
                                    {
                                        if (td.DataSources[i - 1].type != "sqlite")
                                        {
                                            v = v.Replace($"${{datasource{i}-url}}", td.DataSources[i - 1].BuildConnectionString(true));
                                            v = v.Replace($"${{datasource{i}-noauth-url}}", td.DataSources[i - 1].BuildConnectionString(false));
                                        }
                                        else
                                        {
                                            v = v.Replace($"${{datasource{i}-url}}", td.DataSources[i - 1].BuildConnectionString(false, false, false));
                                            v = v.Replace($"${{datasource{i}-noauth-url}}", td.DataSources[i - 1].BuildConnectionString(false, false, false));
                                        }

                                        v = v.Replace($"${{datasource{i}-type}}", td.DataSources[i - 1].type);
                                        v = v.Replace($"${{datasource{i}-username}}", td.DataSources[i - 1].username);
                                        v = v.Replace($"${{datasource{i}-password}}", td.DataSources[i - 1].password);
                                        v = v.Replace($"${{datasource{i}-dbname}}", td.DataSources[i - 1].dbname);
                                        v = v.Replace($"${{datasource{i}-host}}", td.DataSources[i - 1].hostname);
                                        v = v.Replace($"${{datasource{i}-port}}", td.DataSources[i - 1].port);
                                        v = v.Replace($"${{datasource{i}-options}}", td.DataSources[i - 1].options);

                                        if (td.DataSources[i - 1].type == "sqlite")
                                        {
                                            if (v.StartsWith(".@"))
                                            {
                                                v = v.Substring(2);
                                            }

                                            if (v.StartsWith("@"))
                                            {
                                                v = v.Substring(1);
                                            }

                                            if (v.EndsWith(":"))
                                            {
                                                v = v.Substring(0, v.Length - 1);
                                            }

                                        }
                                        translated_env[k] = v;
                                    }

                                    td.Environment = translated_env;
                                }

                                foreach (XmlElement xv in xe.SelectNodes("cobol-sources/src"))
                                {
                                    List<string> m = new List<string>();
                                    m.Add(xv.Attributes["name"].Value);
                                    if (xv.HasAttribute("deps") && !String.IsNullOrWhiteSpace(xv.Attributes["deps"].Value))
                                    {
                                        foreach (string dep in xv.Attributes["deps"].Value.Split(','))
                                        {
                                            m.Add(dep.Trim());
                                        }
                                    }

                                    td.CobolModules.Add(m);
                                }

                                foreach (XmlElement xps in xe.SelectNodes("pre-run-sql-file"))
                                {
                                    int idx = xps.HasAttribute("data-source-index") ? Int32.Parse(xps.Attributes["data-source-index"].Value) : 1;
                                    td.PreRunSQLFile.Add(new Tuple<int, string>(idx, xps.InnerText));
                                }
                                foreach (XmlElement xps in xe.SelectNodes("pre-run-drop-table"))
                                {
                                    int idx = xps.HasAttribute("data-source-index") ? Int32.Parse(xps.Attributes["data-source-index"].Value) : 1;
                                    td.PreRunDropTable.Add(new Tuple<int, string>(idx, xps.InnerText));
                                }

                                foreach (XmlElement xps in xe.SelectNodes("pre-run-sql-statement"))
                                {
                                    int ds_idx = xps.HasAttribute("data-source-index") ? Int32.Parse(xps.Attributes["data-source-index"].Value) : 1;
                                    if (xps.HasAttribute("type"))
                                    {
                                        string target_type = xps.Attributes["type"].Value;
                                        string t_ds_type = td.DataSources[ds_idx - 1].type;
                                        if (target_type != t_ds_type)
                                            continue;
                                    }

                                    List<string> stmt_params = new List<string>();
                                    if (xps.HasAttribute("params"))
                                    {
                                        if (!String.IsNullOrWhiteSpace(xps.Attributes["params"].Value))
                                        {
                                            foreach (string p in xps.Attributes["params"].Value.Split(','))
                                            {
                                                stmt_params.Add(p.Trim());
                                            }
                                        }
                                    }
                                    td.PreRunSQLStatement.Add(new Tuple<int, Tuple<string, List<string>>>(ds_idx, new Tuple<string, List<string>>(xps.InnerText, stmt_params)));
                                }

                                foreach (XmlElement xps in xe.SelectNodes("generate-payload"))
                                {
                                    string id = xps.Attributes["id"].Value;
                                    string type = xps.Attributes["type"].Value;
                                    int length = Int32.Parse(xps.Attributes["length"].Value);

                                    switch (type)
                                    {
                                        case "random-bytes":
                                            int min = xps.HasAttribute("min") ? Int32.Parse(xps.Attributes["min"].Value) : 0;
                                            int max = xps.HasAttribute("max") ? Int32.Parse(xps.Attributes["max"].Value) : 255;
                                            byte[] random_data = Utils.RandomBytes(length, min, max);
                                            td.GeneratedPayload[id] = "#" + System.Convert.ToBase64String(random_data);
                                            break;

                                        case "random-string":
                                            string random_string = Utils.RandomString(length);
                                            td.GeneratedPayload[id] = "$" + random_string;
                                            break;

                                        case "byte-sequence":
                                            int start = xps.HasAttribute("start") ? Int32.Parse(xps.Attributes["start"].Value) : 0;
                                            byte[] byte_sequence = new byte[length];
                                            for (int i = 0; i < length; i++)
                                            {
                                                byte_sequence[i] = (byte)start++;
                                            }

                                            td.GeneratedPayload[id] = ">" + System.Convert.ToBase64String(byte_sequence);
                                            break;
                                    }
                                }

                                CompilerConfig2 cc = available_compilers.First(a => a.Key.Item1 == ctype && a.Key.Item2 == arch).Value;
                                if (cc == null)
                                {
                                    throw new Exception("Compiler not found");
                                }

                                td.CompilerConfiguration = cc;

                                foreach (XmlElement xeo in xe.SelectNodes("expected-output/line"))
                                {
                                    if (xeo.HasAttribute("regex") && Boolean.Parse(xeo.Attributes["regex"].Value))
                                    {
                                        td.ExpectedOutput.Add("{{RX}}" + xeo.InnerText);
                                    }
                                    else
                                    {
                                        td.ExpectedOutput.Add(xeo.InnerText);
                                    }
                                }

                                foreach (XmlElement xeo in xe.SelectNodes("expected-preprocessed-file-content/line"))
                                {
                                    if (xeo.HasAttribute("regex") && Boolean.Parse(xeo.Attributes["regex"].Value))
                                    {
                                        td.ExpectedPreprocessedFileContent.Add("{{RX}}" + xeo.InnerText);
                                    }
                                    else
                                    {
                                        td.ExpectedPreprocessedFileContent.Add(xeo.InnerText);
                                    }
                                }


                                var tcd = new TestCaseData(td);
                                tcd.SetName(td.FullName);
                                tcd.SetProperty("DB Type", (td.DataSources.Count > 0) ? td.DataSources[0].type : "N/A");
                                tcd.SetProperty("Architecture", td.Architecture);
                                tcd.SetProperty("Compiler Type", td.CompilerType);
                                data.Add(tcd);
                                test_count++;
                            }
                        }
                    }
                }
                Console.WriteLine("Done reading test data");
                return data;
            }
            catch (Exception ex)
            {
                System.Diagnostics.Debug.WriteLine(ex.Message + "\n" + ex.StackTrace);
                throw;
            }
        }

        internal static string GetClientProvider(string type, string architecture)
        {
            if (available_data_source_clients.Count(a => a.Key.Item1 == type && a.Key.Item2 == architecture) > 0)
                return available_data_source_clients.First(a => a.Key.Item1 == type && a.Key.Item2 == architecture).Value.provider;

            return null;
        }

        internal static string GetClientAdditionalPreprocessParams(string type, string architecture)
        {
            if (available_data_source_clients.Count(a => a.Key.Item1 == type && a.Key.Item2 == architecture) > 0)
                return available_data_source_clients.First(a => a.Key.Item1 == type && a.Key.Item2 == architecture).Value.additional_preprocess_params;

            return null;
        }

        public string GetDisplayName(MethodInfo methodInfo, object[] data)
        {
            if (data == null || data.Length < 1)
                return null;

            GixSqlTestData td = (GixSqlTestData)data[0];

            return string.Format(CultureInfo.CurrentCulture, td.ToString());
        }

        //private static void ReadConfiguration()
        //{
        //    try
        //    {
        //        // if not initialized, we use the embedded test matrix
        //        string testmatrix_config = Environment.GetEnvironmentVariable("GIXTEST_TESTMATRIX_CONFIG");
        //        if (!String.IsNullOrWhiteSpace(testmatrix_config) && !File.Exists(testmatrix_config))
        //            throw new Exception("Invalid value for GIXTEST_TESTMATRIX_CONFIG");

        //        XmlDocument doc = new XmlDocument();

        //        if (!String.IsNullOrWhiteSpace(testmatrix_config)) {
        //            doc.Load(testmatrix_config);
        //            Console.WriteLine("Using test matrix from file " + testmatrix_config);
        //        }
        //        else {
        //            doc.LoadXml(Utils.GetResource("gixsql_test_data.xml"));
        //            Console.WriteLine("Using embedded test matrix");
        //        }

        //        foreach (var xn in doc.SelectNodes("/test-data/environment/variable"))
        //        {
        //            XmlElement xe = (XmlElement)xn;
        //            global_env[xe.Attributes["key"].Value] = xe.Attributes["value"].Value;
        //        }




        //    }
        //    catch (Exception ex)
        //    {

        //        throw ex;
        //    }
        //}
    }
}
