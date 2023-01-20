using gixsql_tests;
using Microsoft.VisualStudio.TestPlatform.PlatformAbstractions.Interfaces;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using Mono.Options;
using System.IO;

namespace gixsql_tests_nunit
{
    public class Runner
    {
        static Dictionary<string, string> results = new Dictionary<string, string>();
        public static int Main(string[] args)
        {
            DateTime start_time = DateTime.Now;

            var tests = TestDataProvider.GetData();

            GixSqlDynamicTestRunner.ResetCounter();

            foreach (TestCaseData tcd in tests)
            {
                GixSqlTestData test = (GixSqlTestData) tcd.OriginalArguments[0];

                if (TestDataProvider.TestVerbose)
                    Console.WriteLine("Running: " + test.FullName);

                try
                {
                    var tr = new GixSqlDynamicTestRunner();
                    tr.Execute((GixSqlTestData)test);
                    results[test.FullName] = "OK";
                }
                catch (Exception ex)
                {
                    Console.WriteLine(ex.Message);
                    results[test.FullName] = "KO";
                }
            }

            int num_results_ok = results.Count(a => a.Value == "KO");
            int num_results_ko = results.Count(a => a.Value == "KO");

            int mlen = results.Select(a => a.Key.Length).Max();
            var orig_color = Console.ForegroundColor;
            foreach (var de in results)
            {
                Console.ForegroundColor = (de.Value == "OK") ? ConsoleColor.Green : ConsoleColor.Red;
                Console.WriteLine("{0}: {1}", de.Key.PadRight(mlen), de.Value);
                Console.ForegroundColor = orig_color;
            }


            if (num_results_ko > 0)
            {
                orig_color = Console.ForegroundColor;
                Console.ForegroundColor = ConsoleColor.Red;
                Console.WriteLine("\nFailed tests:");
                foreach (var de in results)
                {
                    if (de.Value == "OK")
                        continue;

                    Console.WriteLine("{0}: {1}", de.Key.PadRight(mlen), de.Value);

                }
                Console.ForegroundColor = orig_color;
            }

            Console.WriteLine("Run: {0} - Success: {1} - Failed: {2}", results.Count(), num_results_ok, num_results_ko);


            DateTime end_time = DateTime.Now;
            var elapsed = end_time - start_time;

            Console.WriteLine("Elapsed:" + elapsed.ToString("c"));

            return num_results_ko;
        }
    }
}
