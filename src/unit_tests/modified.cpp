#include "test_util.hpp"
#include "utest.h"
#include <algorithm>
#include <efsw/base.hpp>

using namespace efsw_test;

UTEST( Modified, SingleFile ) {
	std::string testDir = getTemporaryDirectory();
	EXPECT_TRUE( createDirectory( testDir ) );

	std::string testFile = testDir + "/test_file.txt";
	EXPECT_TRUE( createFile( testFile, "initial content" ) );

	TestListener listener;
	efsw::FileWatcher fileWatcher( useGeneric, 100 );

	efsw::WatchID watchId = fileWatcher.addWatch( testDir, &listener, true );
	EXPECT_TRUE( watchId > 0 );

	fileWatcher.watch();

	sleepMs( 100 );

	listener.clearEvents();

	EXPECT_TRUE( writeFile( testFile, " modified content" ) );
	EXPECT_TRUE( listener.waitForActions( efsw::Actions::Modified, "test_file.txt" ) );

	fileWatcher.removeWatch( testDir );
	removeDirectory( testDir );
}

UTEST( Modified, MultipleWrites ) {
	std::string testDir = getTemporaryDirectory();
	EXPECT_TRUE( createDirectory( testDir ) );

	std::string testFile = testDir + "/test_file.txt";
	EXPECT_TRUE( createFile( testFile, "line1\n" ) );

	TestListener listener;
	efsw::FileWatcher fileWatcher( useGeneric, 100 );

	efsw::WatchID watchId = fileWatcher.addWatch( testDir, &listener, true );
	EXPECT_TRUE( watchId > 0 );

	fileWatcher.watch();

	sleepMs( 100 );
	listener.clearEvents();

	for ( int i = 0; i < 3; i++ ) {
		EXPECT_TRUE( writeFile( testFile, "line" + std::to_string( i + 2 ) + "\n" ) );
		sleepMs( 50 );
	}

	EXPECT_TRUE( listener.waitForActions( efsw::Actions::Modified, "test_file.txt" ) );

	fileWatcher.removeWatch( testDir );
	removeDirectory( testDir );
}

UTEST( Modified, AppendToFile ) {
	std::string testDir = getTemporaryDirectory();
	EXPECT_TRUE( createDirectory( testDir ) );

	std::string testFile = testDir + "/test_file.txt";
	EXPECT_TRUE( createFile( testFile, "start" ) );

	TestListener listener;
	efsw::FileWatcher fileWatcher( useGeneric, 100 );

	efsw::WatchID watchId = fileWatcher.addWatch( testDir, &listener, true );
	EXPECT_TRUE( watchId > 0 );

	fileWatcher.watch();

	sleepMs( 100 );
	listener.clearEvents();

	EXPECT_TRUE( writeFile( testFile, "_appended" ) );
	EXPECT_TRUE( listener.waitForActions( efsw::Actions::Modified, "test_file.txt" ) );

	fileWatcher.removeWatch( testDir );
	removeDirectory( testDir );
}

#if EFSW_PLATFORM == EFSW_PLATFORM_INOTIFY
// Setting only the modification time of a directory, as tar does after extracting into it, sends
// IN_MODIFY without a name to the directory's own watch, and with its name to its parent's watch.
UTEST( Modified, DirectoryTimesAreReportedInTheirParent ) {
	if ( useGeneric )
		return;

	const std::filesystem::path testDir( getTemporaryDirectory() );
	EXPECT_TRUE( createDirectory( testDir.string() ) );

	std::vector<std::string> names;
	for ( int i = 0; i < 32; i++ ) {
		names.push_back( "d" + std::to_string( i ) );
		EXPECT_TRUE( createDirectory( ( testDir / names.back() ).string() ) );
	}

	PathTestListener listener;
	efsw::FileWatcher fileWatcher( useGeneric, 100 );

	efsw::WatchID watchId = fileWatcher.addWatch( testDir.string(), &listener, true );
	EXPECT_TRUE( watchId > 0 );

	fileWatcher.watch();
	sleepMs( 100 );

	const auto time = std::filesystem::file_time_type::clock::now() - std::chrono::hours( 24 );
	for ( const std::string& name : names )
		std::filesystem::last_write_time( testDir / name, time );
	std::filesystem::last_write_time( testDir, time );

	for ( const std::string& name : names )
		EXPECT_TRUE( listener.waitForEvent( efsw::Actions::Modified, testDir / name ) );
	sleepMs( 200 );

	// The bytes after a nameless event belong to the next one and must not be read as a name.
	for ( const auto& event : listener.getEvents() ) {
		const bool known =
			event.path.parent_path() == testDir.lexically_normal() &&
			std::find( names.begin(), names.end(), event.path.filename().string() ) != names.end();
		EXPECT_TRUE_MSG( known, event.path.string().c_str() );
	}

	fileWatcher.removeWatch( watchId );
	removeDirectory( testDir.string() );
}
#endif
