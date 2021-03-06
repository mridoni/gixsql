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
#include <vector>
#include <map>

//#include "libgixutils_global.h"

class CopyResolver
{
public:
	CopyResolver() {}
	CopyResolver(const std::string& base_dir, const std::vector<std::string>& _copy_dirs);
	CopyResolver(const std::string& base_dir);

	void resetCache();
	void setCopyDirs(const std::vector<std::string>& _copy_dirs);
	void addCopyDir(const std::string &copy_dir);
	void addCopyDirs(const std::vector<std::string> &_copy_dirs);
	void setExtensions(const std::vector<std::string>& _copy_exts);
	std::vector<std::string> &getExtensions() const;
	void setBaseDir(const std::string base_dir);
	std::vector<std::string>& getCopyDirs() const;
	bool resolveCopyFile(const std::string copy_name, std::string &copy_file);
	void setVerbose(bool b);

private:
	std::vector<std::string> copy_dirs;
	std::vector<std::string> copy_exts;
	std::string base_dir;
	std::string hash;

	bool verbose = false;

	std::map<std::string, std::string> resolve_cache;

	bool resolve_from_dir(const std::string& copy_dir, const std::string& copy_name, std::string& copy_file);
};

