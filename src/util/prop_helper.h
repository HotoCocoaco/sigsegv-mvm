

#ifndef _INCLUDE_SIGSEGV_UTIL_FROP_HELPER_H_
#define _INCLUDE_SIGSEGV_UTIL_FROP_HELPER_H_

using DatatableProxyVector = std::vector<std::pair<SendProp *, int>>;
struct PropCacheEntry
{
    int offset = 0;
    fieldtype_t fieldType = FIELD_VOID;
    int arraySize = 1;
    int elementStride = 0;
    DatatableProxyVector usedTables;
};

void WriteProp(void *entity, PropCacheEntry &entry, variant_t &variant, int arrayPos, int vecAxis);
void ReadProp(void *entity, PropCacheEntry &entry, variant_t &variant, int arrayPos, int vecAxis);

void GetSendPropInfo(SendProp *prop, PropCacheEntry &entry, int offset);
void GetDataMapInfo(typedescription_t &desc, PropCacheEntry &entry);

PropCacheEntry &GetSendPropOffset(ServerClass *serverClass, const std::string &name);
PropCacheEntry &GetDataMapOffset(datamap_t *datamap, const std::string &name);

void ResetPropDataCache();
#endif