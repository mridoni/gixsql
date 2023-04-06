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
class ESQLParserData;

enum class TransformationStepDataType
{
	NotSet = 0,
	Filename = 1,
	ESQLParseData = 2
};

class TransformationStepData {

public:
	TransformationStepData() {}
	~TransformationStepData()
	{
		if (_type == TransformationStepDataType::Filename && _data != nullptr) {
			free(_data);
		}
	}

	void setType(TransformationStepDataType t) { _type = t; }
	void setFilename(const std::string& s) {
		_data = strdup(s.c_str());
	}

	bool isValid() {
		return _data != nullptr;
	}

	ESQLParserData* parserData() { return (ESQLParserData *) _data;  }

	std::string filename()
	{
		switch (_type)
		{
		case TransformationStepDataType::Filename:
			return std::string((char*)_data);
			
		default:
			return string();
		}
	}

	std::string string()
	{
		switch (_type)
		{
			case TransformationStepDataType::Filename:
				return std::string((char*)_data);

			case TransformationStepDataType::ESQLParseData:
				return "(binary data)";

			default:
				return "N/A";
		}
	}

private:
	void* _data = nullptr;
	TransformationStepDataType _type = TransformationStepDataType::NotSet; 


};

class ITransformationStep
{
public:

	virtual ~ITransformationStep() {}

	virtual TransformationStepDataType getInputType() = 0;
	virtual TransformationStepDataType getOutputType() = 0;
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

