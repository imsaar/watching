#ifndef OTA_H
#define OTA_H

void otaSetup();
void otaLoop();
void otaValidateApp();
bool otaRollback();

#endif
