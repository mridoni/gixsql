#pragma once

#include <memory>

#include "gix_esql_driver.hh"
#include "ITransformationStep.h"
#include "TPESQLCommon.h"

class TPESQLParser : public ITransformationStep
{
public:
	TPESQLParser(GixPreProcessor* gpp);

	bool run(std::shared_ptr<ITransformationStep> prev_step) override;
	TransformationStepDataType getInputType() override;
	TransformationStepDataType getOutputType() override;

private:

	std::shared_ptr<ESQLParserData> parser_data;

	gix_esql_driver main_module_driver;
};

