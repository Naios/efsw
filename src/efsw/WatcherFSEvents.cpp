#include <efsw/WatcherFSEvents.hpp>
#include <efsw/FileWatcherFSEvents.hpp>
#include <efsw/FileSystem.hpp>
#include <efsw/Debug.hpp>

#if EFSW_PLATFORM == EFSW_PLATFORM_FSEVENTS

namespace efsw {

WatcherFSEvents::WatcherFSEvents() :
	Watcher(),
	Parent( NULL ),
	FWatcher( NULL )
{
}

WatcherFSEvents::WatcherFSEvents( WatchID id, std::string directory, FileWatchListener * listener, bool recursive, WatcherFSEvents * parent ) :
	Watcher( id, directory, listener, recursive ),
	Parent( parent ),
	FWatcher( NULL )
{
}

WatcherFSEvents::~WatcherFSEvents()
{
	FSEventStreamStop( FSStream );
    FSEventStreamInvalidate( FSStream );
    FSEventStreamRelease( FSStream );
}

bool WatcherFSEvents::inParentTree( WatcherFSEvents * parent )
{
	WatcherFSEvents * tNext = Parent;

	while ( NULL != tNext )
	{
		if ( tNext == parent )
		{
			return true;
		}

		tNext = tNext->Parent;
	}

	return false;
}

void WatcherFSEvents::init()
{
	CFStringRef CFDirectory			= CFStringCreateWithCString( NULL, Directory.c_str(), kCFStringEncodingUTF8 );
	CFArrayRef	CFDirectoryArray	= CFArrayCreate( NULL, (const void **)&CFDirectory, 1, NULL );
	
	Uint32 streamFlags = kFSEventStreamCreateFlagNone;
	
	if ( FileWatcherFSEvents::isGranular() )
	{
		streamFlags = efswFSEventStreamCreateFlagFileEvents;
	}
	
	FSEventStreamContext ctx;
	/* Initialize context */
	ctx.version = 0;
	ctx.info = this;
	ctx.retain = NULL;
	ctx.release = NULL;
	ctx.copyDescription = NULL;

	FSStream = FSEventStreamCreate( kCFAllocatorDefault, &FileWatcherFSEvents::FSEventCallback, &ctx, CFDirectoryArray, kFSEventStreamEventIdSinceNow, 0, streamFlags );

	FWatcher->mNeedInit.push_back( this );

	CFRelease( CFDirectoryArray );
	CFRelease( CFDirectory );
}

void WatcherFSEvents::initAsync()
{
	FSEventStreamScheduleWithRunLoop( FSStream, FWatcher->mRunLoopRef, kCFRunLoopDefaultMode );
	FSEventStreamStart( FSStream );
}

void WatcherFSEvents::handleAction( const std::string& path, const Uint32& flags )
{
	static std::string	lastRenamed = "";
	static bool			lastWasAdd = false;

	if ( flags & (	kFSEventStreamEventFlagUserDropped |
					kFSEventStreamEventFlagKernelDropped |
					kFSEventStreamEventFlagEventIdsWrapped |
					kFSEventStreamEventFlagHistoryDone |
					kFSEventStreamEventFlagMount |
					kFSEventStreamEventFlagUnmount |
					kFSEventStreamEventFlagRootChanged) )
	{
		return;
	}

	if ( !Recursive )
	{
		/** In case that is not recursive the watcher, ignore the events from subfolders */
		if ( path.find_last_of( FileSystem::getOSSlash() ) != Directory.size() - 1 )
		{
			return;
		}
	}
	
	std::string dirPath( FileSystem::pathRemoveFileName( path ) );
	std::string filePath( FileSystem::fileNameFromPath( path ) );
	
	if ( FileWatcherFSEvents::isGranular() )
	{
		efDEBUG( "Event in: %s - flags: %ld\n", path.c_str(), flags );

		if ( !( flags & efswFSEventStreamEventFlagItemRenamed ) )
		{
			if ( flags & efswFSEventStreamEventFlagItemCreated )
			{
				Listener->handleFileAction( ID, dirPath, filePath, Actions::Add );
			}

			if ( flags & kFSEventsModified )
			{
				Listener->handleFileAction( ID, dirPath, filePath, Actions::Modified );
			}

			if ( flags & efswFSEventStreamEventFlagItemRemoved )
			{
				Listener->handleFileAction( ID, dirPath, filePath, Actions::Delete );
			}
		}
		else
		{
			if ( lastRenamed.empty() )
			{
				efDEBUG( "New lastRenamed: %s\n", filePath.c_str() );

				lastRenamed	= path;
				lastWasAdd	= FileInfo::exists( path );

				if ( flags & efswFSEventStreamEventFlagItemCreated )
				{
					Listener->handleFileAction( ID, dirPath, filePath, Actions::Add );
				}

				if ( flags & kFSEventsModified )
				{
					Listener->handleFileAction( ID, dirPath, filePath, Actions::Modified );
				}

				if ( flags & efswFSEventStreamEventFlagItemRemoved )
				{
					Listener->handleFileAction( ID, dirPath, filePath, Actions::Delete );
				}
			}
			else
			{
				std::string oldDir( FileSystem::pathRemoveFileName( lastRenamed ) );
				std::string newDir( FileSystem::pathRemoveFileName( path ) );
				std::string oldFilepath( FileSystem::fileNameFromPath( lastRenamed ) );

				if ( lastRenamed != path )
				{
					if ( oldDir == newDir )
					{
						if ( !lastWasAdd )
						{
							Listener->handleFileAction( ID, dirPath, filePath, Actions::Moved, oldFilepath );
						}
						else
						{
							Listener->handleFileAction( ID, dirPath, oldFilepath, Actions::Moved, filePath );
						}
					}
					else
					{
						Listener->handleFileAction( ID, oldDir, oldFilepath, Actions::Delete );
						Listener->handleFileAction( ID, dirPath, filePath, Actions::Add );

						if ( flags & kFSEventsModified )
						{
							Listener->handleFileAction( ID, dirPath, filePath, Actions::Modified );
						}
					}
				}
				else
				{
					if ( flags & efswFSEventStreamEventFlagItemCreated )
					{
						Listener->handleFileAction( ID, dirPath, filePath, Actions::Add );
					}

					if ( flags & kFSEventsModified )
					{
						Listener->handleFileAction( ID, dirPath, filePath, Actions::Modified );
					}

					if ( flags & efswFSEventStreamEventFlagItemRemoved )
					{
						Listener->handleFileAction( ID, dirPath, filePath, Actions::Delete );
					}
				}

				lastRenamed.clear();
			}
		}
	}
	else
	{
		
	}
}

} 

#endif
