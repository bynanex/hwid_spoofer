#include "pattern.hpp"
#include "util.hpp"
#include "raid_extension.hpp"
#include <classpnp.h>
#include <ntifs.h>

NTSTATUS driver_start()
{
	PDRIVER_OBJECT disk_object = nullptr;
	util::reference_driver_by_name( L"\\Driver\\Disk", &disk_object );

	if ( disk_object == nullptr )
		return STATUS_UNSUCCESSFUL;
	
	memory::initialize( L"disk.sys" );
	const auto DiskEnableDisableFailurePrediction = reinterpret_cast< NTSTATUS( __fastcall* )( PFUNCTIONAL_DEVICE_EXTENSION, BOOLEAN ) >( memory::from_pattern( "\x48\x89\x5c\x24\x00\x48\x89\x74\x24\x00\x57\x48\x81\xec\x00\x00\x00\x00\x48\x8b\x05\x00\x00\x00\x00\x48\x33\xc4\x48\x89\x84\x24\x00\x00\x00\x00\x48\x8b\x59\x60\x48\x8b\xf1\x40\x8a\xfa\x8b\x4b\x10", "xxxx?xxxx?xxxx????xxx????xxxxxxx????xxxxxxxxxxxxx" ) );

	if ( PVOID(DiskEnableDisableFailurePrediction) == nullptr )
		return STATUS_UNSUCCESSFUL;
	
	memory::initialize( L"storport.sys" );
	const auto RaidUnitRegisterInterfaces_address = memory::from_pattern( "\xe8\x00\x00\x00\x00\x48\x8b\xcb\xe8\x00\x00\x00\x00\x85\xc0\x74\x0a", "x????xxxx????xxxx" );

	if ( PVOID( RaidUnitRegisterInterfaces_address ) == nullptr )
		return STATUS_UNSUCCESSFUL;
	
	const auto RaidUnitRegisterInterfaces = reinterpret_cast< NTSTATUS( __fastcall* )( RAID_UNIT_EXTENSION* ) >( RaidUnitRegisterInterfaces_address + 5 + *reinterpret_cast< std::int32_t* >( RaidUnitRegisterInterfaces_address + 1 ) );

	auto device_object_count = 0ul;
	IoEnumerateDeviceObjectList( disk_object, nullptr, 0, &device_object_count );

	LARGE_INTEGER seed_large{};
	KeQuerySystemTimePrecise( &seed_large );

	const auto seed = seed_large.LowPart ^ seed_large.HighPart;
	auto current_object = disk_object->DeviceObject;

	for (auto i = 0u; i < device_object_count; i++)
	{
		const auto fd_extension = static_cast<PFUNCTIONAL_DEVICE_EXTENSION>( current_object->DeviceExtension );

		if ( !fd_extension )
		{
			current_object = current_object->NextDevice;
			continue;
		}
		
		const auto fs_device = IoGetDeviceAttachmentBaseRef( current_object );

		if ( !fs_device || fs_device->DeviceType != FILE_DEVICE_DISK )
		{
			current_object = current_object->NextDevice;
			continue;
		}
		
		const auto raid_extension = static_cast<PRAID_UNIT_EXTENSION>( fs_device->DeviceExtension );

		if ( !raid_extension )
		{
			current_object = current_object->NextDevice;
			continue;
		}
		
		PSTOR_SCSI_IDENTITY identity = nullptr;

		switch ( util::get_windows_version( ) )
		{
			case 1709:
			case 1803:
				identity = reinterpret_cast<PSTOR_SCSI_IDENTITY>( std::uintptr_t( raid_extension ) + 0x60 );
				break;
			case 1809:
			case 1903:
				identity = reinterpret_cast<PSTOR_SCSI_IDENTITY>( std::uintptr_t( raid_extension ) + 0x68 );
				break;
			default:
				break;
		}
		
		if ( !identity )
		{
			current_object = current_object->NextDevice;
			continue;
		}

		const auto fdo_descriptor = fd_extension->DeviceDescriptor;

		if ( !fdo_descriptor )
		{
			current_object = current_object->NextDevice;
			continue;
		}
		
		const auto fdo_serial = fdo_descriptor->SerialNumberOffset + reinterpret_cast<char*>( fdo_descriptor );

		serializer::randomize( seed, fdo_serial );

		identity->SerialNumber.Length = static_cast<USHORT>( std::strlen( fdo_serial ) );
		memset( identity->SerialNumber.Buffer, 0, identity->SerialNumber.Length );
		memcpy( identity->SerialNumber.Buffer, fdo_serial, identity->SerialNumber.Length );
		
		DiskEnableDisableFailurePrediction( fd_extension, FALSE );
		RaidUnitRegisterInterfaces( raid_extension );

		current_object = current_object->NextDevice;
	}
	
	return STATUS_SUCCESS;
}
