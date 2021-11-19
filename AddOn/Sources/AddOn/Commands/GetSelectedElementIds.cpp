#include "GetSelectedElementIds.hpp"
#include "ResourceIds.hpp"
#include "ObjectState.hpp"


namespace AddOnCommands {

constexpr const char* SelectedElementIdsField = "selectedElementIds";


GS::String GetSelectedElementIds::GetNamespace () const
{
	return CommandNamespace;
}


GS::String GetSelectedElementIds::GetName () const
{
	return GetSelectedElementIdsCommandName;
}
	
		
GS::Optional<GS::UniString> GetSelectedElementIds::GetSchemaDefinitions () const
{
	return GS::NoValue; 
}


GS::Optional<GS::UniString>	GetSelectedElementIds::GetInputParametersSchema () const
{
	return GS::NoValue; 
}


GS::Optional<GS::UniString> GetSelectedElementIds::GetResponseSchema () const
{
	return GS::NoValue; 
}


API_AddOnCommandExecutionPolicy GetSelectedElementIds::GetExecutionPolicy () const 
{
	return API_AddOnCommandExecutionPolicy::ScheduleForExecutionOnMainThread; 
}


GS::Array<API_Guid> GetSelectedElementGuids ()
{
	GSErrCode				err;
	GS::Array<API_Guid>		elementGuids;
	API_SelectionInfo		selectionInfo;
	GS::Array<API_Neig>		selNeigs;

	err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, true);
	if (err == NoError) {
		if (selectionInfo.typeID != API_SelEmpty) {
			for (const API_Neig& neig : selNeigs)
				elementGuids.Push (neig.guid);
		}
	}

	BMKillHandle ((GSHandle *) &selectionInfo.marquee.coords);

	return elementGuids;
}


GS::ObjectState GetSelectedElementIds::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{

	GS::Array<API_Guid>	elementGuids = GetSelectedElementGuids ();

	GS::ObjectState retVal;

	const auto& listAdder = retVal.AddList<GS::UniString> (SelectedElementIdsField);
	for (const API_Guid& guid : elementGuids) {
		listAdder (APIGuidToString (guid));
	}

	return retVal;
}


void GetSelectedElementIds::OnResponseValidationFailed (const GS::ObjectState& /*response*/) const
{
}


}