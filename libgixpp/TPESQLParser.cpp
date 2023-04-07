#include "TPESQLParser.h"

TPESQLParser::TPESQLParser(GixPreProcessor* gpp) : ITransformationStep(gpp)
{
	parser_data = std::make_shared<ESQLParserData>();

	main_module_driver.setParser(this);
	owner = gpp;
}

bool TPESQLParser::run(std::shared_ptr<ITransformationStep> prev_step)
{
	if (!input->isValid()) {
		if (!prev_step || prev_step->getOutput())
			return false;

		input = prev_step->getOutput();
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

	std::string ps = std::get<std::string>(owner->getOpt("params_style", std::string("d")));
	if (ps == "d")
		parser_data->job_params()->opt_params_style = ESQL_ParameterStyle::DollarPrefix;
	else
		if (ps == "c")
			parser_data->job_params()->opt_params_style = ESQL_ParameterStyle::ColonPrefix;
		else
			if (ps == "a")
				parser_data->job_params()->opt_params_style = ESQL_ParameterStyle::Anonymous;
			else
				parser_data->job_params()->opt_params_style = ESQL_ParameterStyle::Unknown;

	parser_data->job_params()->opt_preprocess_copy_files = std::get<bool>(owner->getOpt("preprocess_copy_files", false));


	int rc = main_module_driver.parse(input, parser_data);
	if (rc == 0) {
		output = new TransformationStepData();
		output->setType(TransformationStepDataType::ESQLParserData);
		output->setParserData(parser_data);
	}
	return rc == 0;
}

TransformationStepDataType TPESQLParser::getInputType()
{
	return TransformationStepDataType::Filename;
}

TransformationStepDataType TPESQLParser::getOutputType()
{
	return TransformationStepDataType::ESQLParserData;
}
