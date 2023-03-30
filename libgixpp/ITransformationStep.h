/*
This file is part of Gix-IDE, an IDE and platform for GnuCOBOL
Copyright (C) 2021 Marco Ridoni

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or (at
your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
USA.
*/

#pragma once

#include <string>

#define SET_ERR(I,S) owner->err_data.err_code = I; owner->err_data.err_messages.push_back(S)

class ITransformationStep;
class GixPreProcessor;

enum class TransformationStepDataType
{
	NotSet = 0,
	Filename = 1,
	ESQLParseData = 2
};

struct TransformationStepData {

	TransformationStepData() {}
	~TransformationStepData() {}

	TransformationStepDataType type = TransformationStepDataType::NotSet; 
	union transformation_step_data {
		constexpr transformation_step_data() {}
		~transformation_step_data() {}
		void* parse_data = nullptr;
		std::string filename;
	} data;

	bool isValid() {
		switch (type)
		{
			case TransformationStepDataType::Filename:
				return !data.filename.empty();

			case TransformationStepDataType::ESQLParseData:
				return data.parse_data != nullptr;

			default:
				return false;
		}
	}

	std::string toString()
	{
		switch (type)
		{
			case TransformationStepDataType::Filename:
				return data.filename;

			case TransformationStepDataType::ESQLParseData:
				return "(binary data)";

			default:
				return "N/A";
		}
	}
};

class ITransformationStep
{
public:

	virtual ~ITransformationStep() {}

	virtual bool run(ITransformationStep* prev_step) = 0;
	virtual TransformationStepData* getInput();
	virtual TransformationStepData* getOutput(ITransformationStep* me = nullptr);

	virtual void setInput(TransformationStepData *input);
	virtual void setOutput(TransformationStepData* output);

	GixPreProcessor *getOwner();


protected:

	ITransformationStep(GixPreProcessor* gpp);

	GixPreProcessor* owner = nullptr;
	TransformationStepData* input = nullptr;
	TransformationStepData* output = nullptr;

};

