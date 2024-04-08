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

#include "AsiLoader.h"

#include <dlfcn.h>
#include <link.h>
#include <sys/mman.h>

#include <algorithm>
#include <chrono>

#include "log.h"
#include "bass_sym.h"
#include "base_bin.h"

using namespace std::chrono_literals;

AsiLoader g_instance;

AsiLoader::AsiLoader() {
	// Init info of this library
	Dl_info info;
	dladdr( (void *)&BASS_Init, &info );
	m_base = (uintptr_t)info.dli_fbase;
	std::string_view fullPath( info.dli_fname );
	m_path = fullPath.substr( 0, fullPath.rfind( '/' ) + 1 );
	if ( m_path.starts_with( "/data/app" ) ) {
		// /data/app/package.name-HASH==/lib/ARCH/
		m_pkg = fullPath.substr( 0, fullPath.rfind( "==" ) - 23 );
		m_pkg = m_pkg.substr( m_pkg.rfind( '/' ) + 1 );
	} else {
		// /data/data/package.name/lib/
		m_pkg = fullPath.substr( 0, fullPath.rfind( "/lib/" ) );
		m_pkg = m_pkg.substr( m_pkg.rfind( '/' ) + 1 );
	}

	VERBOSE( "library address - 0x%X", m_base );
	VERBOSE( "library full path - %s", info.dli_fname );
	LOG( "library location - %s", m_path.data() );
	LOG( "package name - %s", m_pkg.data() );

	// Load original bass library
	m_bass = (link_map *)dlopen( ( m_path + "bass.so" ).data(), RTLD_NOW );
	if ( !m_bass ) {
		WARN( "Can't find bass.so in library location. Fallback to internal bass" );
		auto bass = std::filesystem::path( "/data" ) / "data" / m_pkg / "files" / "bass.so";
		LOG( "Extract BASS to - %s", bass.string().data() );
		auto f = fopen( bass.string().data(), "wb" );
		if ( f ) {
			fwrite( libbass_so, libbass_so_len, 1, f );
			fclose( f );
			m_bass = (link_map *)dlopen( bass.string().data(), RTLD_NOW );
		}
		if ( !m_bass ) {
			FATAL( "Failed to load original bass library - %s", dlerror() );
			return;
		}
	}
	if ( m_bass ) {
		VERBOSE( "BASS library link map - 0x%X", m_bass );

		// Patch self ELF to fix BASS function calling
		pathElf();
	}

	if ( isGtaSaPresent() )
		loadPlugins();
	else {
		LOG( "Use async plugin loading" );
		m_loaderThrd = std::thread( [this] { async_loader(); } );
	}
}

AsiLoader::~AsiLoader() {
	if ( m_loaderThrd.joinable() ) m_loaderThrd.join();
	for ( auto &&plugin : m_plugins ) {
		auto closed	 = dlclose( plugin.handle ) == 0;
		auto deleted = std::filesystem::remove( plugin.path );

		if ( !closed || !deleted ) {
			if ( !closed ) ERROR( "Can't unload plugin %s - %s", plugin.path.filename().string().data(), dlerror() );
			if ( !deleted ) ERROR( "Can't delete plugin %s", plugin.path.filename().string().data() );
		} else
			LOG( "Unload plugin %s", plugin.path.filename().string().data() );
	}
}

void AsiLoader::async_loader() {
	for ( int wait = 0;; ++wait ) {
		if ( !isGtaSaPresent() && wait < 5 ) {
			LOG( "Wait when libGTASA.so is present" );
			std::this_thread::sleep_for( 1s );
			continue;
		} else if ( !isGtaSaPresent() )
			ERROR( "Cannot find library libGTASA.so" );

		// Load plugins
		loadPlugins();
		break;
	}
}

void AsiLoader::loadPlugins() {
	LOG( "Load plugins:" );
	m_plugins.reserve( 10 );
	for ( auto &&file :
		  std::filesystem::directory_iterator( std::filesystem::path( "/sdcard" ) / "Android" / "data" / m_pkg ) ) {
		if ( file.path().extension().string() != ".so" ) continue;
		auto target = std::filesystem::path( "/data" ) / "data" / m_pkg / "files" / file.path().filename();
		if ( std::filesystem::remove( target ) ) VERBOSE( "Update plugin %s", target.filename().string().data() );
		if ( !std::filesystem::copy_file( file.path(), target ) ) {
			WARN( "Can't copy plugin %s", file.path().string().data() );
			continue;
		}
		auto handle = dlopen( target.string().data(), RTLD_NOW );
		if ( !handle )
			WARN( "Can't load plugin %s", target.filename().string().data() );
		else {
			m_plugins.push_back( { target, handle } );
			LOG( "Load plugin %s", target.filename().string().data() );
			if ( !isGtaSaPresent() ) continue;
			auto JNI_OnLoad = (int ( * )( struct JavaVM *, void ** ))dlsym( handle, "JNI_OnLoad" );
			if ( JNI_OnLoad ) {
				if ( !GetJavaVM && !globalVM ) {
					WARN( "Plugin %s contain JNI_OnLoad function, but game does not provide GetJavaVM/globalVM",
						  target.filename().string().data() );
					Dl_info info;
					dladdr( (void *)JNI_OnLoad, &info );
					if ( strstr( info.dli_fname, "libcleo.so" ) ) {
						WARN( "Fallback for CLEO library - direct caling initialize function" );
						( ( void ( * )() )( uintptr_t( info.dli_fbase ) + 0x5A3D ) )();
					}
				} else {
					VERBOSE( "Calling JNI_OnLoad for plugin %s", target.filename().string().data() );
					void *reserve = nullptr;
					auto  jni_ver = JNI_OnLoad( GetJavaVM ? GetJavaVM() : *globalVM, &reserve );
					VERBOSE( "Plugin %s use JNI v%X ", target.filename().string().data(), jni_ver );
				}
			}
		}
	}
	std::reverse( m_plugins.begin(), m_plugins.end() );
	LOG( "Tatal plugins loaded: %d", m_plugins.size() );
}

bool AsiLoader::isGtaSaPresent() {
	auto gtasa = dlopen( ( m_path + "libGTASA.so" ).data(), RTLD_NOLOAD );
	if ( gtasa ) {
		if ( !GetJavaVM ) GetJavaVM = (decltype( GetJavaVM ))dlsym( gtasa, "GetJavaVM" );
		if ( !globalVM ) globalVM = (decltype( globalVM ))dlsym( gtasa, "globalVM" );
		dlclose( gtasa );
		return true;
	}
	gtasa = (void *)fallbackGetLibrary( "libGTASA.so" );
	if ( gtasa ) {
		if ( !GetJavaVM ) GetJavaVM = (decltype( GetJavaVM ))fallbackGetSymbol( (uint8_t *)gtasa, "GetJavaVM" );
		if ( !globalVM ) globalVM = (decltype( globalVM ))fallbackGetSymbol( (uint8_t *)gtasa, "globalVM" );
		return true;
	}
	return false;
}

void AsiLoader::pathElf() {
	auto lib = (uint8_t *)m_base;
	auto hdr = (Elf32_Ehdr *)lib;
	auto pht = (Elf32_Phdr *)( &lib[hdr->e_phoff] );

	Elf32_Dyn *dyn = nullptr;
	for ( int i = 0; i < hdr->e_phnum; ++i, pht++ ) {
		if ( pht->p_type != 2 ) continue;
		dyn = (Elf32_Dyn *)( &lib[pht->p_paddr] );
		break;
	}
	if ( !dyn ) {
		ERROR( "Failed to fix self ELF - DYNAMIC section not found" );
		return;
	}

	uintptr_t off_str = 0;
	uintptr_t off_sym = 0;
	for ( ; dyn->d_tag != 0; dyn++ ) {
		if ( dyn->d_tag == 5 )
			off_str = dyn->d_un.d_val;
		else if ( dyn->d_tag == 6 )
			off_sym = dyn->d_un.d_val;
	}
	if ( !off_str || !off_sym ) {
		ERROR( "Failed to fix self ELF - DYNAMIC section do not contain reqired tables" );
		return;
	}

	auto sym = (Elf32_Sym *)&lib[off_sym];
	for ( uint32_t i = 0, j = 0; j < g_bassCount; ++i, sym++ ) {
		auto name_ptr = (const char *)( sym->st_name + lib + off_str );
		if ( strncmp( name_ptr, "BASS_", 5 ) ) continue;
		++j;
		auto bass_sym = (uintptr_t)dlsym( m_bass, name_ptr );
		if ( !bass_sym ) {
			WARN( "BASS do not contain symbol %s[%d/%d]", name_ptr, j, g_bassCount );
			continue;
		}
		if ( mprotect( (void *)( uintptr_t( sym ) & 0xFFFFF000 ), PAGE_SIZE, PROT_READ | PROT_WRITE ) ) {
			ERROR( "Failed to fix self ELF - can't add write access to symbol table" );
			return;
		}
		sym->st_value = bass_sym - m_base;
		VERBOSE( "fix offset to %s[%d/%d]", name_ptr, j, g_bassCount );
	}
}

uintptr_t AsiLoader::fallbackGetLibrary( std::string_view name ) {
	auto fd = fopen( "/proc/self/maps", "rt" );
	if ( fd == 0 ) {
		fclose( fd );
		return 0;
	}

	char buf[1024]{ 0 };
	while ( fgets( buf, sizeof( buf ), fd ) ) {
		if ( name != buf ) continue;
		fclose( fd );
		return std::stoul( buf, nullptr, 16 );
	}

	fclose( fd );
	return 0;
}

uintptr_t AsiLoader::fallbackGetSymbol( const uint8_t *lib, std::string_view name ) {
	auto hdr = (Elf32_Ehdr *)lib;
	auto pht = (Elf32_Phdr *)( &lib[hdr->e_phoff] );

	Elf32_Dyn *dyn = nullptr;
	for ( int i = 0; i < hdr->e_phnum; ++i, pht++ ) {
		if ( pht->p_type != 2 ) continue;
		dyn = (Elf32_Dyn *)( &lib[pht->p_paddr] );
		break;
	}
	if ( !dyn ) return 0;

	uint32_t off_str = 0;
	uint32_t sz_str	 = 0;
	uint32_t off_sym = 0;
	for ( ; dyn->d_tag != 0; dyn++ ) {
		if ( dyn->d_tag == 5 )
			off_str = dyn->d_un.d_val;
		else if ( dyn->d_tag == 6 )
			off_sym = dyn->d_un.d_val;
		else if ( dyn->d_tag == 10 )
			sz_str = dyn->d_un.d_val;
	}
	if ( !off_str || !off_sym || !sz_str ) return 0;

	auto sym = (Elf32_Sym *)&lib[off_sym];
	for ( uint32_t i = 0; off_sym + i * sizeof( Elf32_Sym ) != off_str; ++i, sym++ ) {
		auto name_ptr = (const char *)( sym->st_name + lib + off_str );
		if ( uintptr_t( name_ptr ) < uintptr_t( lib ) + off_str ) break;
		if ( uintptr_t( name_ptr ) >= uintptr_t( lib ) + off_str + sz_str ) break;
		if ( name != name_ptr ) continue;

		return uintptr_t( lib ) + sym->st_value;
	}

	return 0;
}
