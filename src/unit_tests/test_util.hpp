#ifndef EFSW_TEST_UTIL_HPP
#define EFSW_TEST_UTIL_HPP

#include <chrono>
#include <condition_variable>
#include <efsw/efsw.hpp>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined( _WIN32 )
#include <process.h>
#else
#include <unistd.h>
#endif

namespace efsw_test {

class TestListener : public efsw::FileWatchListener {
  public:
	std::mutex mtx;
	std::condition_variable cv;
	std::vector<std::tuple<efsw::Action, std::string, std::string>> events;

	void handleFileAction( efsw::WatchID, const std::string& /* dir */, const std::string& filename,
						   efsw::Action action, const std::string& oldFilename = "" ) override {
		std::lock_guard<std::mutex> lock( mtx );
		events.push_back( std::make_tuple( action, filename, oldFilename ) );
		cv.notify_one();
	}

	void clearEvents() {
		std::lock_guard<std::mutex> lock( mtx );
		events.clear();
	}

	bool checkEvent( efsw::Action expectedAction, const std::string& expectedFilename,
					 const std::string& expectedOldFilename = "" ) {
		std::lock_guard<std::mutex> lock( mtx );
		for ( const auto& ev : events ) {
			if ( std::get<0>( ev ) == expectedAction && std::get<1>( ev ) == expectedFilename &&
				 std::get<2>( ev ) == expectedOldFilename ) {
				return true;
			}
		}
		return false;
	}

	size_t getEventCount() {
		std::lock_guard<std::mutex> lock( mtx );
		return events.size();
	}

	std::vector<std::tuple<efsw::Action, std::string, std::string>> getEvents() {
		std::lock_guard<std::mutex> lock( mtx );
		return events;
	}

	bool waitForActions( efsw::Actions::Action action, const std::string& filename,
						 int timeoutMs = 2000 ) {
		std::unique_lock<std::mutex> lock( mtx );
		return cv.wait_for( lock, std::chrono::milliseconds( timeoutMs ), [&] {
			for ( const auto& ev : events ) {
				if ( std::get<0>( ev ) == action && std::get<1>( ev ) == filename )
					return true;
			}
			return false;
		} );
	}
};

/// Records every event with the path it was reported for.
class PathTestListener : public efsw::FileWatchListener {
  public:
	struct Event {
		efsw::Action action;
		std::filesystem::path path;
	};

	/// How long each event keeps the watch's thread busy, like a listener with work to do.
	std::chrono::milliseconds delay{ 0 };

	void handleFileAction( efsw::WatchID, const std::string& dir, const std::string& filename,
						   efsw::Action action, const std::string& = "" ) override {
		std::this_thread::sleep_for( delay );
		std::lock_guard<std::mutex> lock( mtx );
		events.push_back(
			Event{ action, ( std::filesystem::path( dir ) / filename ).lexically_normal() } );
		cv.notify_all();
	}

	void handleMissedFileActions( efsw::WatchID, const std::string& ) override {
		std::lock_guard<std::mutex> lock( mtx );
		missedFileActions = true;
		cv.notify_all();
	}

	std::vector<Event> getEvents() {
		std::lock_guard<std::mutex> lock( mtx );
		return events;
	}

	bool hasMissedFileActions() {
		std::lock_guard<std::mutex> lock( mtx );
		return missedFileActions;
	}

	bool hasEvent( efsw::Action action, const std::filesystem::path& path ) {
		std::lock_guard<std::mutex> lock( mtx );
		return containsEvent( action, path.lexically_normal() );
	}

	bool waitForEvent( efsw::Action action, const std::filesystem::path& path,
					   int timeoutMs = 2000 ) {
		const std::filesystem::path expected( path.lexically_normal() );
		std::unique_lock<std::mutex> lock( mtx );
		return cv.wait_for( lock, std::chrono::milliseconds( timeoutMs ),
							[&] { return containsEvent( action, expected ); } );
	}

  private:
	std::mutex mtx;
	std::condition_variable cv;
	std::vector<Event> events;
	bool missedFileActions = false;

	bool containsEvent( efsw::Action action, const std::filesystem::path& path ) const {
		for ( const Event& event : events ) {
			if ( event.action == action && event.path == path )
				return true;
		}
		return false;
	}
};

inline std::string getTemporaryDirectory() {
#if defined( _WIN32 )
	const auto processId = _getpid();
#else
	const auto processId = getpid();
#endif
	return std::filesystem::temp_directory_path().string() + "/efsw_test_" +
		   std::to_string( processId ) + "_" +
		   std::to_string( std::hash<std::thread::id>{}( std::this_thread::get_id() ) );
}

inline bool createDirectory( const std::string& path ) {
	std::error_code ec;
	return std::filesystem::create_directory( path, ec ) || ec.value() == 0;
}

inline bool removeDirectory( const std::string& path ) {
	std::error_code ec;
	std::filesystem::remove_all( path, ec );
	return true;
}

inline bool createFile( const std::string& path, const std::string& content = "" ) {
	std::ofstream f( path );
	if ( !f )
		return false;
	if ( !content.empty() ) {
		f << content;
	}
	f.close();
	return true;
}

inline bool createFileFs( const std::filesystem::path& path, const std::string& content = "" ) {
	std::ofstream f( path );
	if ( !f )
		return false;
	if ( !content.empty() ) {
		f << content;
	}
	f.close();
	return true;
}

inline bool removeFile( const std::string& path ) {
	std::error_code ec;
	std::filesystem::remove( path, ec );
	return !ec;
}

inline bool writeFile( const std::string& path, const std::string& content ) {
	std::ofstream f( path, std::ios::app );
	if ( !f )
		return false;
	f << content;
	f.close();
	return true;
}

inline bool renameFile( const std::string& oldPath, const std::string& newPath ) {
	std::error_code ec;
	std::filesystem::rename( oldPath, newPath, ec );
	return !ec;
}

inline void sleepMs( int ms ) {
	std::this_thread::sleep_for( std::chrono::milliseconds( ms ) );
}

extern bool useGeneric;

} // namespace efsw_test

#endif
