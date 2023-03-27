#pragma once

#include "gix_esql_driver.hh"
#include "ITransformationStep.h"
#include "TPESQLCommon.h"

class TPESQLParser : public ITransformationStep
{
public:
	TPESQLParser(GixPreProcessor* gpp);

	bool run(ITransformationStep* prev_step) override;

private:

	
	ESQLJobParams params;
	ESQLParserData parser_data;

	//gix_esql_driver main_module_driver;
};

