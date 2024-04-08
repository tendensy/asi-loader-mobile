/*
 * <one line to give the program's name and a brief idea of what it does.>
 * Copyright (C) 2021  SR_team me@sr.team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ASILOADER_H
#define ASILOADER_H

#include <string>
#include <vector>
#include <filesystem>
#include <string_view>
#include <thread>
#include <cstdint>

/**
 * @todo write docs
 */
class AsiLoader {
	struct Plugin {
		std::filesystem::path path;
		void *				  handle;
	};

	std::string m_path;
	std::string m_pkg;

	uintptr_t		 m_base;
	struct link_map *m_bass;

	std::thread			m_loaderThrd;
	std::vector<Plugin> m_plugins;

	typedef struct JavaVM *( *GetJavaVM_t )();
	GetJavaVM_t		GetJavaVM = nullptr;
	struct JavaVM **globalVM  = nullptr;

public:
	/**
     * Default constructor
     */
	AsiLoader();

	/**
     * Destructor
     */
	~AsiLoader();

protected:
	void async_loader();
	void loadPlugins();
	bool isGtaSaPresent();

	void pathElf();

	uintptr_t fallbackGetLibrary( std::string_view name );
	uintptr_t fallbackGetSymbol( const uint8_t *lib, std::string_view name );
};

#endif // ASILOADER_H
