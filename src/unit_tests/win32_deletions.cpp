#include <efsw/base.hpp>

#if EFSW_PLATFORM == EFSW_PLATFORM_WIN32

// Ahead of utest.h, which declares QueryPerformanceCounter itself without it.
#include <windows.h>

#include "test_util.hpp"
#include "utest.h"
#include <algorithm>

using namespace efsw_test;

namespace {

// Deletes like libc++ does, without POSIX semantics: an item that another handle keeps open, as a
// watch keeps its directory, stays pending deletion until that handle is closed.
bool removeWithoutPosixSemantics( const std::filesystem::path& path ) {
	HANDLE handle = CreateFileW(
		path.c_str(), DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
		OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, NULL );
	if ( handle == INVALID_HANDLE_VALUE )
		return false;

	FILE_DISPOSITION_INFO disposition = { TRUE };
	const bool marked = SetFileInformationByHandle( handle, FileDispositionInfo, &disposition,
													sizeof( disposition ) ) != 0;
	CloseHandle( handle );
	return marked;
}

// Deletes a tree deepest entry first, like libc++'s remove_all.
bool removeTreeWithoutPosixSemantics( const std::filesystem::path& path ) {
	if ( std::filesystem::is_directory( std::filesystem::symlink_status( path ) ) ) {
		for ( const auto& entry : std::filesystem::directory_iterator( path ) ) {
			if ( !removeTreeWithoutPosixSemantics( entry.path() ) )
				return false;
		}
	}
	return removeWithoutPosixSemantics( path );
}

std::vector<efsw::WatcherOption> crossDirectoryMoves() {
	return { { efsw::Options::ReportCrossDirectoryMoves, 1 } };
}

} // namespace

// With ReportCrossDirectoryMoves a recursive watch holds every deletion back for a moment, to pair
// it with the addition of a move. Deleting the watched directory ends its read.
UTEST( Win32Deletions, HeldDeletionsArriveWhenTheWatchedDirectoryGoes ) {
	if ( useGeneric )
		return;

	const std::filesystem::path testDir( getTemporaryDirectory() );
	const std::filesystem::path watched( testDir / "watched" );
	ASSERT_TRUE( createDirectory( testDir.string() ) );
	ASSERT_TRUE( createDirectory( watched.string() ) );
	EXPECT_TRUE( createFileFs( watched / "a.txt", "a" ) );
	EXPECT_TRUE( createFileFs( watched / "b.txt", "b" ) );

	PathTestListener listener;
	efsw::FileWatcher fileWatcher( useGeneric, 100 );

	efsw::WatchID watchId =
		fileWatcher.addWatch( watched.string(), &listener, true, crossDirectoryMoves() );
	EXPECT_TRUE( watchId > 0 );

	fileWatcher.watch();
	sleepMs( 100 );

	EXPECT_TRUE( removeWithoutPosixSemantics( watched / "a.txt" ) );
	EXPECT_TRUE( removeWithoutPosixSemantics( watched / "b.txt" ) );
	// Long enough for the watch to read them, too short for it to give up pairing them.
	sleepMs( 30 );
	EXPECT_TRUE( removeWithoutPosixSemantics( watched ) );

	EXPECT_TRUE( listener.waitForEvent( efsw::Actions::Delete, watched / "a.txt" ) );
	EXPECT_TRUE( listener.waitForEvent( efsw::Actions::Delete, watched / "b.txt" ) );

	fileWatcher.removeWatch( watchId );
	removeDirectory( testDir.string() );
}

// A path that comes back right after its deletion, a file replaced by a directory or a file saved
// over by renaming a new one onto it, is deleted before it is back.
UTEST( Win32Deletions, AHeldDeletionComesBeforeItsPathIsBack ) {
	if ( useGeneric )
		return;

	const std::filesystem::path testDir( getTemporaryDirectory() );
	ASSERT_TRUE( createDirectory( testDir.string() ) );
	EXPECT_TRUE( createFileFs( testDir / "replaced", "a file" ) );
	EXPECT_TRUE( createFileFs( testDir / "saved.txt", "old" ) );

	PathTestListener listener;
	efsw::FileWatcher fileWatcher( useGeneric, 100 );

	efsw::WatchID watchId =
		fileWatcher.addWatch( testDir.string(), &listener, true, crossDirectoryMoves() );
	EXPECT_TRUE( watchId > 0 );

	fileWatcher.watch();
	sleepMs( 100 );

	EXPECT_TRUE( removeFile( ( testDir / "replaced" ).string() ) );
	EXPECT_TRUE( createDirectory( ( testDir / "replaced" ).string() ) );
	EXPECT_TRUE( createFileFs( testDir / "saved.txt.new", "new" ) );
	EXPECT_TRUE( removeFile( ( testDir / "saved.txt" ).string() ) );
	EXPECT_TRUE(
		renameFile( ( testDir / "saved.txt.new" ).string(), ( testDir / "saved.txt" ).string() ) );

	EXPECT_TRUE( listener.waitForEvent( efsw::Actions::Add, testDir / "replaced" ) );
	EXPECT_TRUE( listener.waitForEvent( efsw::Actions::Moved, testDir / "saved.txt" ) );
	// Longer than a deletion is held.
	sleepMs( 300 );

	auto deletedLast = [&]( const std::filesystem::path& path ) {
		bool deleted = false;
		for ( const auto& event : listener.getEvents() ) {
			if ( event.path != path.lexically_normal() )
				continue;
			if ( event.action == efsw::Actions::Delete )
				deleted = true;
			else if ( event.action == efsw::Actions::Add || event.action == efsw::Actions::Moved )
				deleted = false;
		}
		return deleted;
	};
	EXPECT_FALSE( deletedLast( testDir / "replaced" ) );
	EXPECT_FALSE( deletedLast( testDir / "saved.txt" ) );

	fileWatcher.removeWatch( watchId );
	removeDirectory( testDir.string() );
}

// The watched directory deleted entry by entry stays pending deletion while the watch holds it,
// and its read ends. Windows drops what changed since the last completed read then.
UTEST( Win32Deletions, AWatchedDirectoryDeletedEntryByEntryIsToldAndLetGo ) {
	if ( useGeneric )
		return;

	for ( const bool pairMoves : { false, true } ) {
		const std::filesystem::path testDir( getTemporaryDirectory() );
		const std::filesystem::path watched( testDir / "watched" );
		ASSERT_TRUE( createDirectory( testDir.string() ) );
		ASSERT_TRUE( createDirectory( watched.string() ) );
		ASSERT_TRUE( createDirectory( ( watched / "sub" ).string() ) );
		ASSERT_TRUE( createDirectory( ( watched / "sub" / "deeper" ).string() ) );
		const std::vector<std::filesystem::path> entries = {
			watched / "a.txt",
			watched / "e.txt",
			watched / "sub",
			watched / "sub" / "b.txt",
			watched / "sub" / "c.txt",
			watched / "sub" / "deeper",
			watched / "sub" / "deeper" / "d.txt",
		};
		for ( const auto& entry : entries ) {
			if ( !std::filesystem::exists( entry ) )
				EXPECT_TRUE( createFileFs( entry, entry.filename().string() ) );
		}

		PathTestListener listener;
		listener.delay = std::chrono::milliseconds( 2 );
		efsw::FileWatcher fileWatcher( useGeneric, 100 );

		efsw::WatchID watchId = fileWatcher.addWatch(
			watched.string(), &listener, true,
			pairMoves ? crossDirectoryMoves() : std::vector<efsw::WatcherOption>() );
		EXPECT_TRUE( watchId > 0 );

		fileWatcher.watch();
		sleepMs( 100 );

		EXPECT_TRUE( removeTreeWithoutPosixSemantics( watched ) );

		// Every deletion arrives, or the listener is told that it missed some.
		bool told = false;
		for ( int i = 0; i < 300 && !told; i++ ) {
			told = listener.hasMissedFileActions() ||
				   std::all_of( entries.begin(), entries.end(), [&]( const auto& entry ) {
					   return listener.hasEvent( efsw::Actions::Delete, entry );
				   } );
			if ( !told )
				sleepMs( 10 );
		}
		EXPECT_TRUE( told );

		// The watch lets the deleted directory go, it can be created again.
		bool createdAgain = false;
		for ( int i = 0; i < 300 && !createdAgain; i++ ) {
			createdAgain = CreateDirectoryW( watched.c_str(), NULL ) != 0;
			if ( !createdAgain )
				sleepMs( 10 );
		}
		EXPECT_TRUE( createdAgain );

		fileWatcher.removeWatch( watchId );
		removeDirectory( testDir.string() );
	}
}

#endif
