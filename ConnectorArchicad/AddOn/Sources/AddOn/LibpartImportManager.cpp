#include "LibpartImportManager.hpp"

#include "BuiltInLibrary.hpp"
#include "FileSystem.hpp"
#include "Folder.hpp"
#include "GSUnID.hpp"
#include "MD5Channel.hpp"
#include "AttributeManager.hpp"
#include "Box3DData.h"


LibpartImportManager* LibpartImportManager::instance = nullptr;

LibpartImportManager* LibpartImportManager::GetInstance ()
{
	if (nullptr == instance) {
		instance = new LibpartImportManager ();
	}
	return instance;
}


void LibpartImportManager::DeleteInstance ()
{
	if (nullptr != instance) {
		delete instance;
		instance = nullptr;
	}
}


LibpartImportManager::LibpartImportManager () : runningNumber(1)
{
	AttributeManager::GetInstance ()->GetDefaultMaterial (defaultMaterialAttribute, defaultMaterialName);
	GetLocation (true, libraryFolderLocation);
}


LibpartImportManager::~LibpartImportManager ()
{
	if (libraryFolderLocation != nullptr)
		delete libraryFolderLocation;
}


static GSErrCode CreateSubFolder (const GS::UniString& name, IO::Location& location)
{
	GSErrCode err = NoError;

	if (location.IsEmpty () || name.IsEmpty ())
		return err;

	GSTimeRecord gsTimeRecord;
	GSTime gsTime (0);

	TIGetTimeRecord (gsTime, &gsTimeRecord, TI_CURRENT_TIME);
	GS::UniString folderNamePostfix (GS::UniString::SPrintf ("%04u%02u%02u-%02u%02u%02u",
		(UInt32) gsTimeRecord.year, (UInt32) gsTimeRecord.month, (UInt32) gsTimeRecord.day,
		(UInt32) gsTimeRecord.hour,	(UInt32) gsTimeRecord.minute, (UInt32) gsTimeRecord.second));

	IO::Name folderName (name);
	folderName.Append ("_");
	folderName.Append (folderNamePostfix);

	IO::Folder folder (location);
	err = folder.CreateFolder (folderName);
	if (err != NoError)
		return err;

	location.AppendToLocal (folderName);

	return err;
}



GSErrCode LibpartImportManager::GetLibpartFromCache (const GS::Array<GS::UniString> modelIds,
	API_LibPart& libPart)
{
	GS::UInt64 checksum = GenerateFingerPrint (modelIds);
	GSErrCode err = Error;
	if (cache.Get (checksum, &libPart)) {
		err = NoError;
	}

	return err;
}


GSErrCode LibpartImportManager::GetLibpart (const ModelInfo& modelInfo,
	AttributeManager& attributeManager,
	API_LibPart& libPart)
{
	GS::UInt64 checksum = GenerateFingerPrint (modelInfo.GetIds ());
	GSErrCode err = Error;
	if (cache.Get (checksum, &libPart)) {
		err = NoError;
	} else {
		err = CreateLibraryPart (modelInfo, attributeManager, libPart);
		if (err == NoError)
			cache.Add (checksum, libPart);
	}

	return err;
}


GSErrCode LibpartImportManager::CreateLibraryPart (const ModelInfo& modelInfo,
	AttributeManager& attributeManager,
	API_LibPart& libPart)
{
	GSErrCode err = NoError;
	BNZeroMemory (&libPart, sizeof (API_LibPart));
	libPart.typeID = APILib_ObjectID;
	libPart.isTemplate = false;
	libPart.isPlaceable = true;
	libPart.location = libraryFolderLocation;

	// todo use Speckle types here
	const GS::UnID unID = BL::BuiltInLibraryMainGuidContainer::GetInstance ().GetUnIDWithNullRevGuid (BL::BuiltInLibPartID::BuildingElementLibPartID);
	CHCopyC (unID.ToUniString ().ToCStr (), libPart.parentUnID);

	GS::ucscpy (libPart.docu_UName, GS::UniString::SPrintf ("Speckle Object %u", runningNumber++).ToUStr ());

	ACAPI_Environment (APIEnv_OverwriteLibPartID, (void*) (Int32) true, nullptr);
	err = ACAPI_LibPart_Create (&libPart);
	ACAPI_Environment (APIEnv_OverwriteLibPartID, (void*) (Int32) false, nullptr);
	if (err == NoError) {
		API_LibPartSection section;
		GS::String line;
#ifdef ServerMainVers_2600
		line.EnsureCapacity(1000);
#endif
		// Comment script section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_SectComText;
		ACAPI_LibPart_NewSection (&section);
		line = "Speckle";
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		ACAPI_LibPart_EndSection ();

		// Keyword section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_SectKeywords;
		ACAPI_LibPart_NewSection (&section);
		line = "Speckle";
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		ACAPI_LibPart_EndSection ();

		// Copyright section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_SectCopyright;
		ACAPI_LibPart_NewSection (&section);
		line = "Speckle Systems";	// Author
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		ACAPI_LibPart_EndSection ();

		// Master script section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_Sect1DScript;
		ACAPI_LibPart_NewSection (&section);
		line = "";
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		ACAPI_LibPart_EndSection ();

		// 3D script section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_Sect3DScript;
		ACAPI_LibPart_NewSection (&section);

		line = GS::String::SPrintf("!%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("! 3D Script (Generated by Speckle Connector)%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("!%s%s", GS::EOL, GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());

		line = GS::String::SPrintf ("defaultResolution = 36%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("RESOL defaultResolution%s%s", GS::EOL, GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());

		line = GS::String::SPrintf ("hiddenProfileEdge = 0%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("hiddenBodyEdge = 0%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("smoothBodyEdge = 0%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("visibleBodyEdge = 262144%s%s", GS::EOL, GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());

		line = GS::String::SPrintf ("IF (GLOB_CONTEXT %% 10 >= 2 AND GLOB_CONTEXT %% 10 <= 4) AND showOnlyContourEdgesIn3D <> 0 THEN%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("\thiddenProfileEdge = 1%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("\thiddenBodyEdge = 1%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("\tsmoothBodyEdge = 2%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("ENDIF%s%s", GS::EOL, GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());

		line = GS::String::SPrintf ("!%s%s", GS::EOL, GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("RESOL defaultResolution%s%s", GS::EOL, GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("XFORM map_xform[1][1], map_xform[2][1], map_xform[3][1], map_xform[4][1],%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("\tmap_xform[1][2], map_xform[2][2], map_xform[3][2], map_xform[4][2],%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("\tmap_xform[1][3], map_xform[2][3], map_xform[3][3], map_xform[4][3]%s%s", GS::EOL, GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());

		// create material
		GS::Array<ModelInfo::Material> materials = modelInfo.GetMaterials ();
		for (const auto& material : materials) {
			API_Attribute materialAttribute;
			attributeManager.GetMaterial (material, materialAttribute);
		}

		GS::Array<ModelInfo::Vertex> vertices = modelInfo.GetVertices ();
		Box3D box = Box3D::CreateEmpty ();
		for (UInt32 i = 0; i < vertices.GetSize (); i++) {
			line = GS::String::SPrintf ("VERT %f, %f, %f\t!#%d%s", vertices[i].GetX (), vertices[i].GetY (), vertices[i].GetZ (), i + 1, GS::EOL);
			ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
			box.Extend (Point3D (vertices[i].GetX (), vertices[i].GetY (), vertices[i].GetZ ()));
		}

		line = GS::String::SPrintf ("%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());

		const GS::HashTable<ModelInfo::EdgeId, ModelInfo::EdgeData>& edges = modelInfo.GetEdges ();

		UInt32 edgeIndex = 1;
		UInt32 polygonIndex = 1;
		GS::Array<ModelInfo::Polygon> polygons = modelInfo.GetPolygons ();
		for (const auto& polygon : polygons) {
			const GS::Array<Int32>& pointIds = polygon.GetPointIds ();
			UInt32 pointsCount = pointIds.GetSize ();

			GS::UniString materialName = defaultMaterialName;
			{
				ModelInfo::Material material;
				if (NoError == modelInfo.GetMaterial (polygon.GetMaterial (), material)) {
					materialName = material.GetName ();
				}
			}

			line = GS::String::SPrintf ("! Polygon #%d%s%sMATERIAL \"%s\"%s", polygonIndex, GS::EOL, GS::EOL, materialName.ToCStr ().Get (), GS::EOL);
			ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());

			for (UInt32 i = 0; i < pointsCount; i++) {
				Int32 start = i;
				Int32 end = i == pointIds.GetSize () - 1 ? 0 : i + 1;

				Int32 startPointId = pointIds[start], endPointId = pointIds[end];
				ModelInfo::EdgeId edge (startPointId, endPointId);

				bool smooth = false;
				bool hidden = true;
				if (edges.ContainsKey(edge)) {
					ModelInfo::EdgeData edgeData = edges.Get (edge);
		
					switch (edgeData.edgeStatus) {
					case ModelInfo::HiddenEdge:
						break;
					case ModelInfo::SmoothEdge:
						hidden = false;
						smooth = true;
						break;
					case ModelInfo::VisibleEdge:
						hidden = false;
						break;
					default:
						break;
					}
				}

				line = GS::String::SPrintf ("EDGE %d, %d, -1, -1, %s\t!#%d%s", pointIds[start] + 1, pointIds[end] + 1, (smooth ? "smoothBodyEdge" : (hidden ? "hiddenBodyEdge" : "visibleBodyEdge")), edgeIndex + i, GS::EOL);
				ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
			}

			line = GS::String::SPrintf ("PGON %d, 0, -1", pointsCount);
			ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());

			for (UInt32 i = 0; i < pointsCount; i++) {
				line = GS::String::SPrintf (",%s%d", ((i + 1) % 10 == 0 ? GS::EOL : " "), edgeIndex + i);
				ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
			}

			line = GS::String::SPrintf ("\t!#%d%s%s", polygonIndex, GS::EOL, GS::EOL);
			ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());

			edgeIndex += pointsCount;
			polygonIndex++;
		}

		line = GS::String::SPrintf ("BODY 4%s%s", GS::EOL, GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());

		line = GS::String::SPrintf ("HOTSPOT %f, %f, %f%s", box.GetMinX (), box.GetMinY (), box.GetMinZ (), GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("HOTSPOT %f, %f, %f%s", box.GetMinX (), box.GetMinY (), box.GetMaxZ (), GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("HOTSPOT %f, %f, %f%s", box.GetMinX (), box.GetMaxY (), box.GetMinZ (), GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("HOTSPOT %f, %f, %f%s", box.GetMinX (), box.GetMaxY (), box.GetMaxZ (), GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("HOTSPOT %f, %f, %f%s", box.GetMaxX (), box.GetMinY (), box.GetMinZ (), GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("HOTSPOT %f, %f, %f%s", box.GetMaxX (), box.GetMinY (), box.GetMaxZ (), GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("HOTSPOT %f, %f, %f%s", box.GetMaxX (), box.GetMaxY (), box.GetMinZ (), GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("HOTSPOT %f, %f, %f%s%s", box.GetMaxX (), box.GetMaxY (), box.GetMaxZ (), GS::EOL, GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());

		line = "DEL TOP";
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());

		ACAPI_LibPart_EndSection ();

		// 2D script section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_Sect2DScript;
		ACAPI_LibPart_NewSection (&section);
		line = GS::String::SPrintf ("!%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("! 2D Script (Generated by Speckle Connector)%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("!%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("PEN gs_cont_pen%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("SET FILL gs_fill_type%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		line = GS::String::SPrintf ("PROJECT2{2} 3, 270.0, 3+32, gs_back_pen, 0, 0, 0%s", GS::EOL);
		ACAPI_LibPart_WriteSection (line.GetLength(), line.ToCStr());
		ACAPI_LibPart_EndSection ();

		// Parameter script section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_SectVLScript;
		ACAPI_LibPart_NewSection (&section);
		ACAPI_LibPart_EndSection ();

		// Parameters section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_SectParamDef;

		short nPars = 3;
		API_AddParType** addPars = reinterpret_cast<API_AddParType**>(BMAllocateHandle (nPars * sizeof (API_AddParType), ALLOCATE_CLEAR, 0));
		if (addPars != nullptr) {
			API_AddParType* pAddPar = &(*addPars)[0];
			pAddPar->typeID = APIParT_Boolean;
			pAddPar->typeMod = 0;
			CHTruncate ("showOnlyContourEdgesIn3D", pAddPar->name, sizeof (pAddPar->name));
			GS::ucscpy (pAddPar->uDescname, L ("Show Only Contour Edges In 3D"));
			pAddPar->value.real = 1;

			pAddPar = &(*addPars)[1];
			pAddPar->typeID = APIParT_RealNum;
			pAddPar->typeMod = API_ParArray;
			pAddPar->dim1 = 4;
			pAddPar->dim2 = 3;
			CHTruncate ("map_xform", pAddPar->name, sizeof (pAddPar->name));
			GS::ucscpy (pAddPar->uDescname, L ("General Transformation"));
			pAddPar->value.array = BMAllocateHandle (pAddPar->dim1 * pAddPar->dim2 * sizeof (double), ALLOCATE_CLEAR, 0);
			double** arrHdl = reinterpret_cast<double**>(pAddPar->value.array);
			for (Int32 k = 0; k < pAddPar->dim1; k++)
				for (Int32 j = 0; j < pAddPar->dim2; j++)
					(*arrHdl)[k * pAddPar->dim2 + j] = (k == j ? 1.0 : 0.0);

			pAddPar = &(*addPars)[2];
			pAddPar->typeID = APIParT_Boolean;
			CHTruncate ("AC_show2DHotspotsIn3D", pAddPar->name, sizeof (pAddPar->name));
			GS::ucscpy (pAddPar->uDescname, L ("Show 2D Hotspots in 3D"));
			pAddPar->value.real = 0;

			double aa = 1.0;
			double bb = 1.0;
			GSHandle sectionHdl = nullptr;
			ACAPI_LibPart_GetSect_ParamDef (&libPart, addPars, &aa, &bb, nullptr, &sectionHdl);

			API_LibPartDetails details;
			BNZeroMemory (&details, sizeof (API_LibPartDetails));
			details.object.autoHotspot = false;
			details.object.fixSize = true;
			ACAPI_LibPart_SetDetails_ParamDef (&libPart, sectionHdl, &details);

			ACAPI_LibPart_AddSection (&section, sectionHdl, nullptr);

			BMKillHandle (reinterpret_cast<GSHandle*>(&arrHdl));
			BMKillHandle (reinterpret_cast<GSHandle*>(&addPars));
			BMKillHandle (&sectionHdl);
		} else {
			err = APIERR_MEMFULL;
		}

		// Save the constructed library part
		if (err == NoError)
			err = ACAPI_LibPart_Save (&libPart);
	}

	return err;
}


GSErrCode LibpartImportManager::GetLocation (bool useEmbeddedLibrary, IO::Location*& libraryFolderLocation) const
{
	GS::Array<API_LibraryInfo> libInfo;
	libraryFolderLocation = nullptr;

	if (useEmbeddedLibrary) {
		Int32 embeddedLibraryIndex = -1;
		// get embedded library location
		if (ACAPI_Environment (APIEnv_GetLibrariesID, &libInfo, &embeddedLibraryIndex) == NoError && embeddedLibraryIndex >= 0) {
			try {
				libraryFolderLocation = new IO::Location (libInfo[embeddedLibraryIndex].location);
			} catch (std::bad_alloc&) {
				return APIERR_MEMFULL;
			}

			if (libraryFolderLocation != nullptr) {
				CreateSubFolder ("Speckle Library", *libraryFolderLocation);
			}
		}
	} else {
		// register our own folder and create the library part in it
		if (ACAPI_Environment (APIEnv_GetLibrariesID, &libInfo) == NoError) {
			IO::Location folderLoc;
			API_SpecFolderID specID = API_UserDocumentsFolderID;
			ACAPI_Environment (APIEnv_GetSpecFolderID, &specID, &folderLoc);
			folderLoc.AppendToLocal (IO::Name ("Speckle Library"));
			IO::Folder destFolder (folderLoc, IO::Folder::Create);
			if (destFolder.GetStatus () != NoError || !destFolder.IsWriteable ())
				return APIERR_GENERAL;

			libraryFolderLocation = new IO::Location (folderLoc);

			for (UInt32 ii = 0; ii < libInfo.GetSize (); ii++) {
				if (folderLoc == libInfo[ii].location)
					return NoError;
			}

			try {
				API_LibraryInfo li;
				li.location = folderLoc;

				libInfo.Push (li);
			} catch (const GS::OutOfMemoryException&) {
				DBBREAK_STR ("Not enough memory");
				return APIERR_MEMFULL;
			}

			ACAPI_Environment (APIEnv_SetLibrariesID, &libInfo);
		}
	}

	return NoError;
}


GS::UInt64 LibpartImportManager::GenerateFingerPrint (const GS::Array<GS::UniString>& hashIds) const
{
	IO::MD5Channel md5Channel;
	MD5::FingerPrint checkSum;

	for (const GS::UniString& id : hashIds)
		md5Channel.Write (id);

	md5Channel.Finish (&checkSum);
	return checkSum.GetUInt64Value ();
}
