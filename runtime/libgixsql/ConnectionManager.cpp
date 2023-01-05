/*
* This file is part of Gix-IDE, an IDE and platform for GnuCOBOL
* Copyright (C) 2021 Marco Ridoni
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public License
* as published by the Free Software Foundation; either version 3,
* or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; see the file COPYING.LIB.  If
* not, write to the Free Software Foundation, 51 Franklin Street, Fifth Floor
* Boston, MA 02110-1301 USA
*/


#include <string>
#include <vector>
#include <map>
#include <algorithm>

#include "ConnectionManager.h"
#include "DbInterfaceFactory.h"
#include "utils.h"

#define GIXSQL_DEFAULT_CONN_PREFIX "DEFAULT"

static std::vector<std::shared_ptr<Connection>> _connections;
static std::map<int, std::shared_ptr<Connection>> _connection_map;
static std::map<std::string, std::shared_ptr<Connection>> _connection_name_map;

static int next_conn_id = 1;

ConnectionManager::ConnectionManager()
{
}


ConnectionManager::~ConnectionManager()
{
	clear();
}

std::shared_ptr<Connection> ConnectionManager::create()
{
	return std::make_shared<Connection>();
}

std::shared_ptr<Connection> ConnectionManager::get(const std::string& name)
{
	std::string n = trim_copy(name);

	if (n.empty())
		return default_connection;

	if (_connection_name_map.find(n) != _connection_name_map.end())
		return _connection_name_map[n];

	return nullptr;
}

int ConnectionManager::add(std::shared_ptr<Connection> conn)
{
	conn->id = ++next_conn_id;
	if (conn->name.empty()) {
		conn->name = GIXSQL_DEFAULT_CONN_PREFIX + std::to_string(conn->id);
		if (this->default_connection && starts_with(this->default_connection->name, GIXSQL_DEFAULT_CONN_PREFIX)) {
			spdlog::warn("terminating current default connection (probably still open): {}", default_connection->name);
			ConnectionManager::remove(this->default_connection);
		}

		this->default_connection = conn;
	}

	_connections.push_back(conn);
	_connection_map[conn->id] = conn;
	_connection_name_map[conn->name] = conn;

	return conn->id;
}

void ConnectionManager::remove(std::shared_ptr<Connection> conn)
{
	if (conn == NULL)
		return;

	int id = conn->id;
	std::string name = conn->name;
	_connections.erase(std::remove(_connections.begin(), _connections.end(), conn), _connections.end());
	_connection_map.erase(id);
	_connection_name_map.erase(name);

	if (conn == default_connection)
		default_connection.reset();
}

bool ConnectionManager::exists(const std::string& cname)
{
	return _connection_name_map.find(cname) != _connection_name_map.end();
}

std::vector<std::shared_ptr<Connection>> ConnectionManager::list()
{
	return _connections;
}

void ConnectionManager::clear()
{	
	default_connection.reset();
	_connections.clear();
	_connection_map.clear();
	_connection_name_map.clear();	
}
