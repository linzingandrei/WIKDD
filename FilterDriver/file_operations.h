#pragma once
#include "fltKernel.h"

FLT_PREOP_CALLBACK_STATUS PreCreate(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Out_ PVOID* Buffer
);