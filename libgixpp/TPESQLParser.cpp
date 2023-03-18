#include "TPESQLParser.h"

TPESQLParser::TPESQLParser(GixPreProcessor* gpp) : ITransformationStep(gpp)
{

}

bool TPESQLParser::run(ITransformationStep* prev_step)
{
	if (input_file.empty()) {
		if (!prev_step || prev_step->getOutput().empty())
			return false;

		input_file = prev_step->getOutput();
	}

#if defined(_WIN32) && defined(_DEBUG) && defined(VERBOSE)
	char bfr[512];
	sprintf(bfr, "********************\nStarting run, processing file %s\n", input_file.c_str());
	OutputDebugStringA(bfr);
#endif

#if _DEBUG_LOG_ON
	char dbg_bfr[512];
#if defined(_WIN32)
	OutputDebugStringA("TPESQLProcessor invoked\n===============\n");
#else
	fprintf(stderr, "TPESQLProcessor invoked\n===============\n");
#endif

	for (auto it = owner->getOpts().begin(); it != owner->getOpts().end(); ++it) {
		sprintf(dbg_bfr, "%s = %s\n", it.key().toLocal8Bit().data(), it.value().toString().toLocal8Bit().data());
#if defined(_WIN32)
		OutputDebugStringA(dbg_bfr);
#else
		fprintf(stderr, dbg_bfr);
#endif

	}
	for (auto cd : owner->getCopyResolver()->getCopyDirs()) {
		sprintf(dbg_bfr, "COPY path: %s\n", cd.toLocal8Bit().data());
#if defined(_WIN32)
		OutputDebugStringA(dbg_bfr);
#else
		fprintf(stderr, dbg_bfr);
#endif
	}
#if defined(_WIN32)
	OutputDebugStringA("===============\n");
#else
	fprintf(stderr, "TPESQLProcessor invoked\n===============\n");
#endif
#endif


	main_module_driver.opt_params_style = params.opt_params_style;
	main_module_driver.opt_preprocess_copy_files = params.opt_preprocess_copy_files;

	main_module_driver.setCaller(this);


	int rc = main_module_driver.parse(owner, input_file);
}
