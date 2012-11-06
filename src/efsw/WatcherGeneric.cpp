#include <efsw/WatcherGeneric.hpp>
#include <efsw/FileSystem.hpp>
#include <efsw/DirWatcherGeneric.hpp>

namespace efsw
{

WatcherGeneric::WatcherGeneric( WatchID id, const std::string& directory, FileWatchListener * fwl, FileWatcherImpl * fw, bool recursive ) :
	Watcher( id, directory, fwl, recursive ),
	WatcherImpl( fw ),
	DirWatch( NULL ),
	CurDirWatch( NULL )
{
	FileSystem::dirAddSlashAtEnd( Directory );

	DirWatch = CurDirWatch	= new DirWatcherGeneric( this, directory, recursive );

	DirWatch->addChilds();
}

WatcherGeneric::~WatcherGeneric()
{
	efSAFE_DELETE( DirWatch );
}

void WatcherGeneric::watch()
{
	DirWatch->watch();
}

bool WatcherGeneric::pathInWatches( std::string path )
{
	return DirWatch->pathInWatches( path );
}

}
