#pragma once

#include "ITransformationStep.h"

class TPESQLParser : public ITransformationStep
{
public:
	TPESQLParser(GixPreProcessor* gpp);

	bool run(ITransformationStep* prev_step) override;
};

