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

#include <memory>
#include <string>

#define SET_ERR(I,S) owner->err_data.err_code = I; owner->err_data.err_messages.push_back(S)

class ITransformationStep;
class GixPreProcessor;
class ESQLParserData;

enum class TransformationStepDataType
{
	NotSet = 0,
	Filename = 1,
	ESQLParserData = 2
};

class TransformationStepData {

public:
	TransformationStepData() {}
	~TransformationStepData()
	{
	}

	void setType(TransformationStepDataType t) { _type = t; }
	void setFilename(const std::string& s) {
		_filename = s;
	}

	void setParserData(std::shared_ptr<ESQLParserData> pd) {
		_parser_data = pd;
	}

	bool isValid() {
		switch (_type)
		{
			case TransformationStepDataType::Filename:
				return !_filename.empty();

			case TransformationStepDataType::ESQLParserData:
			default:
				return (_parser_data.get() != nullptr);
		}
	}

	std::shared_ptr<ESQLParserData> parserData() { return  _parser_data;  }

	std::string filename()
	{
		switch (_type)
		{
		case TransformationStepDataType::Filename:
			return _filename;
			
		default:
			return string();
		}
	}

	std::string string()
	{
		switch (_type)
		{
			case TransformationStepDataType::Filename:
				return _filename;

			case TransformationStepDataType::ESQLParserData:
				return "(binary data)";

			default:
				return "N/A";
		}
	}

private:

	TransformationStepDataType _type = TransformationStepDataType::NotSet;

	std::string _filename;
	std::shared_ptr<ESQLParserData> _parser_data;
	
};

class ITransformationStep
{
public:

	virtual ~ITransformationStep() {}

	virtual TransformationStepDataType getInputType() = 0;
	virtual TransformationStepDataType getOutputType() = 0;
	virtual bool run(std::shared_ptr<ITransformationStep> prev_step) = 0;

	virtual TransformationStepData* getInput();
	virtual TransformationStepData* getOutput(std::shared_ptr<ITransformationStep> me = nullptr);

	virtual void setInput(TransformationStepData *input);
	virtual void setOutput(TransformationStepData* output);

	GixPreProcessor *getOwner();


protected:

	ITransformationStep(GixPreProcessor* gpp);

	GixPreProcessor* owner = nullptr;
	TransformationStepData* input = nullptr;
	TransformationStepData* output = nullptr;

};

