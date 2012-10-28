#include <efsw/FileWatcherInotify.hpp>

#if EFSW_PLATFORM == EFSW_PLATFORM_INOTIFY

#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <sys/inotify.h>
#include <efsw/FileSystem.hpp>
#include <efsw/System.hpp>
#include <efsw/String.hpp>

#define BUFF_SIZE ((sizeof(struct inotify_event)+FILENAME_MAX)*1024)

namespace efsw
{
	FileWatcherInotify::FileWatcherInotify() :
		mFD(-1),
		mThread(NULL)
	{
		mFD = inotify_init();

		if (mFD < 0)
		{
			fprintf (stderr, "Error: %s\n", strerror(errno));
		}
		else
		{
			mInitOK = true;
		}
	}

	FileWatcherInotify::~FileWatcherInotify()
	{
		WatchMap::iterator iter = mWatches.begin();
		WatchMap::iterator end = mWatches.end();

		for(; iter != end; ++iter)
		{
			efSAFE_DELETE( iter->second );
		}

		mWatches.clear();

		if ( mFD != -1 )
		{
			close(mFD);
			mFD = -1;
		}

		if ( mThread )
			mThread->terminate();

		efSAFE_DELETE( mThread );
	}

	WatchID FileWatcherInotify::addWatch(const std::string& directory, FileWatchListener* watcher, bool recursive)
	{
		std::string dir( directory );

		FileSystem::dirAddSlashAtEnd( dir );

		int wd = inotify_add_watch (mFD, dir.c_str(), IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_MOVED_FROM | IN_DELETE);
		if (wd < 0)
		{
			if(errno == ENOENT)
				return Errors::Log::createLastError( Errors::FileNotFound, directory );
			else
				return Errors::Log::createLastError( Errors::Unspecified, std::string(strerror(errno)) );
		}
		
		Watcher * pWatch	= new Watcher();
		pWatch->Listener	= watcher;
		pWatch->ID			= wd;
		pWatch->Directory	= dir;
		pWatch->Recursive	= recursive;
		
		mWatchesLock.lock();
		mWatches.insert(std::make_pair(wd, pWatch));
		mWatchesLock.unlock();

		if ( pWatch->Recursive )
		{
			std::map<std::string, FileInfo> files = FileSystem::filesInfoFromPath( pWatch->Directory );
			std::map<std::string, FileInfo>::iterator it = files.begin();

			for ( ; it != files.end(); it++ )
			{
				FileInfo fi = it->second;

				if ( fi.isDirectory() )
				{
					addWatch( fi.Filepath, watcher, recursive );
				}
			}
		}

		return wd;
	}

	void FileWatcherInotify::removeWatch(const std::string& directory)
	{
		mWatchesLock.lock();

		WatchMap::iterator iter = mWatches.begin();

		for(; iter != mWatches.end(); ++iter)
		{
			if( directory == iter->second->Directory )
			{
				Watcher * watch = iter->second;

				if ( watch->Recursive )
				{
					WatchMap::iterator it = mWatches.begin();

					for(; it != mWatches.end(); ++it)
					{
						if ( watch->ID != it->second->ID && -1 != String::strStartsWith( watch->Directory, it->second->Directory ) )
						{
							removeWatch( it->second->ID );
						}
					}
				}

				mWatches.erase( iter );

				inotify_rm_watch(mFD, watch->ID);

				efSAFE_DELETE( watch );

				return;
			}
		}

		mWatchesLock.unlock();
	}

	void FileWatcherInotify::removeWatch( WatchID watchid )
	{
		mWatchesLock.lock();

		WatchMap::iterator iter = mWatches.find( watchid );

		if( iter == mWatches.end() )
			return;

		Watcher * watch = iter->second;

		if ( watch->Recursive )
		{
			WatchMap::iterator it = mWatches.begin();

			for(; it != mWatches.end(); ++it)
			{
				if ( watch->ID != it->second->ID && -1 != String::strStartsWith( watch->Directory, it->second->Directory ) )
				{
					removeWatch( it->second->ID );
				}
			}
		}

		mWatches.erase( iter );

		inotify_rm_watch(mFD, watchid);
		
		efSAFE_DELETE( watch );

		mWatchesLock.unlock();
	}

	void FileWatcherInotify::watch()
	{
		if ( NULL == mThread )
		{
			mThread = new Thread( &FileWatcherInotify::run, this );
			mThread->launch();
		}
	}

	void FileWatcherInotify::run()
	{
		do
		{
			ssize_t len, i = 0;
			static char buff[BUFF_SIZE] = {0};

			len = read (mFD, buff, BUFF_SIZE);

			if (len != -1)
			{
				while (i < len)
				{
					struct inotify_event *pevent = (struct inotify_event *)&buff[i];

					mWatchesLock.lock();
					Watcher * watch = mWatches[pevent->wd];
					mWatchesLock.unlock();

					handleAction(watch, pevent->name, pevent->mask);
					i += sizeof(struct inotify_event) + pevent->len;
				}
			}
		} while( mFD > 0 );
	}

	void FileWatcherInotify::handleAction(Watcher* watch, const std::string& filename, unsigned long action)
	{
		if(!watch || !watch->Listener)
		{
			return;
		}

		std::string fpath(watch->Directory + filename);

		if(IN_CLOSE_WRITE & action)
		{
			watch->Listener->handleFileAction(watch->ID, watch->Directory, filename,Actions::Modified);
		}

		if(IN_MOVED_TO & action || IN_CREATE & action)
		{
			watch->Listener->handleFileAction(watch->ID, watch->Directory, filename,Actions::Add);

			/// If the watcher is recursive, checks if the new file is a folder, and creates a watcher
			if ( watch->Recursive && FileSystem::isDirectory( fpath ) )
			{
				bool found = false;

				for ( WatchMap::iterator it = mWatches.begin(); it != mWatches.end(); it++ )
				{
					if ( it->second->Directory == fpath )
					{
						found = true;
						break;
					}
				}

				if ( !found )
				{
					addWatch( fpath, watch->Listener, watch->Recursive );
				}
			}
		}

		if(IN_MOVED_FROM & action || IN_DELETE & action)
		{
			watch->Listener->handleFileAction(watch->ID, watch->Directory, filename,Actions::Delete);

			/// If the file erased is a directory and recursive is enabled, removes the directory erased
			if ( watch->Recursive && FileSystem::isDirectory( fpath ) )
			{
				bool found = false;

				for ( WatchMap::iterator it = mWatches.begin(); it != mWatches.end(); it++ )
				{
					if ( it->second->Directory == fpath )
					{
						removeWatch( it->second->ID );
						break;
					}
				}
			}
		}
	}


	std::list<std::string> FileWatcherInotify::directories()
	{
		std::list<std::string> dirs;

		mWatchesLock.lock();

		WatchMap::iterator it = mWatches.begin();

		for ( ; it != mWatches.end(); it++ )
		{
			dirs.push_back( it->second->Directory );
		}

		mWatchesLock.unlock();

		return dirs;
	}
}

#endif
