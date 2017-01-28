#include "ExporterCommon.h"

extern "C" DllExport PREMPLUGENTRY xSDKExport(csSDK_int32 selector, exportStdParms *stdParms, void *param1, void *param2);

prMALError exStartup(exportStdParms *stdParmsP, exExporterInfoRec *infoRecP);
prMALError exBeginInstance(exportStdParms *stdParmsP, exExporterInstanceRec *instanceRecP);
prMALError exEndInstance(exportStdParms *stdParmsP, exExporterInstanceRec *instanceRecP);
prMALError exExport(exportStdParms *stdParmsP, exDoExportRec *exportInfoP);
prMALError exFileExtension(exportStdParms *stdParmsP, exQueryExportFileExtensionRec *exportFileExtensionRecP);