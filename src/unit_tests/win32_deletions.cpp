#include <efsw/base.hpp>

#if EFSW_PLATFORM == EFSW_PLATFORM_WIN32

// Ahead of utest.h, which declares QueryPerformanceCounter itself without it.
#include <windows.h>

#include "test_util.hpp"
#include "utest.h"

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

#endif
