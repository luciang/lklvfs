/**
* Cache Manager Callbacks
**/

#include <lklvfs.h>

//
//	acquire for lazy write
//
BOOLEAN DDKAPI VfsAcqLazyWrite(PVOID context, BOOLEAN wait)
{
	PLKLFCB	fcb = (PLKLFCB) context;

	ASSERT(fcb);
	ASSERT((fcb->id.type == FCB) && (fcb->id.size == sizeof(LKLFCB)));

	return ExAcquireResourceExclusiveLite(&fcb->fcb_resource, wait);
}

//
//	release from lazy write
//
VOID DDKAPI VfsRelLazyWrite(PVOID context)
{
	PLKLFCB	fcb = (PLKLFCB)context;

	ASSERT(fcb);
	ASSERT((fcb->id.type == FCB) && (fcb->id.size == sizeof(PLKLFCB)));

	RELEASE(&fcb->fcb_resource);
}

//
//	acquire for read ahead
//
BOOLEAN DDKAPI VfsAcqReadAhead(PVOID context, BOOLEAN wait)
{
	PLKLFCB fcb = (PLKLFCB) context;

	ASSERT(fcb);
	ASSERT((fcb->id.type == FCB) && (fcb->id.size == sizeof(PLKLFCB)));
	
	return ExAcquireResourceSharedLite(&(fcb->fcb_resource), wait);
}

//
//	release from read ahead
//
VOID DDKAPI VfsRelReadAhead(PVOID context)
{
	PLKLFCB fcb = (PLKLFCB) context;

	ASSERT(fcb);
	ASSERT((fcb->id.type == FCB) && (fcb->id.size == sizeof(PLKLFCB)));

	RELEASE(&(fcb->fcb_resource));
}
