#include <efsw/System.hpp>
#include <efsw/platform/platformimpl.hpp>

namespace efsw {

void System::sleep( const unsigned long& ms )
{
	Platform::System::sleep( ms );
}

} 
