/*
 */

#include "VRC.h"

#include <array>

extern short VRGetShort(const void *base, int byteOffset)
{
    auto *buf = static_cast<const unsigned char *>(base) + byteOffset;
    short vv = buf[0];
    vv |= (buf[1] << 8);

    return (vv);
}

int32_t VRGetInt(const void *base, unsigned int byteOffset)
{
    auto *buf = static_cast<const unsigned char *>(base) + byteOffset;
    int32_t vv = buf[0];
    vv |= (static_cast<int32_t>(buf[1])) << 8U;
    vv |= (static_cast<int32_t>(buf[2])) << 16U;
    vv |= (static_cast<int32_t>(buf[3])) << 24U;
    return (vv);
}

uint32_t VRGetUInt(const void *base, const uint32_t byteOffset)
{
    auto *buf = static_cast<const unsigned char *>(base) + byteOffset;
    auto vv = static_cast<uint32_t>(buf[0]);
    vv |= (static_cast<uint32_t>(buf[1])) << 8U;
    vv |= (static_cast<uint32_t>(buf[2])) << 16U;
    vv |= (static_cast<uint32_t>(buf[3])) << 24U;

    return (vv);
}

///////////////////////////////////////////////////////////////////

int VRReadChar(VSILFILE *fp)
{
    unsigned char buf = 0;
    // size_t ret =
    VSIFReadL(&buf, 1, 1, fp);
    const unsigned char vv = buf;
    // if (ret<1) return (EOF);
    return (vv);
}

int VRReadShort(VSILFILE *fp)
{
    std::array<unsigned char, 2> buf;
    // size_t ret =
    VSIFReadL(&buf, 1, 2, fp);
    short vv = 0;
    vv |= buf[0];
    vv |= (buf[1] << 8);

    // if (ret<2) return (EOF);
    return (vv);
}

int32_t VRReadInt(VSILFILE *fp)
{
    std::array<unsigned char, 4> buf;
    // size_t ret =
    VSIFReadL(&buf, 1, 4, fp);
    signed int vv = 0;
    vv |= buf[0];
    vv |= (buf[1] << 8);
    vv |= (buf[2] << 16);
    vv |= (buf[3] << 24);

    // if (ret<4) return (EOF);
    return (vv);
}

int32_t VRReadInt(VSILFILE *fp, unsigned int byteOffset)
{
    if (VSIFSeekL(fp, byteOffset, SEEK_SET))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "VRReadInt cannot seek to VRC byteOffset %u=x%08x", byteOffset,
                 byteOffset);
        return CE_Failure;  // dangerous ?
    }
    return VRReadInt(fp);
}

uint32_t VRReadUInt(VSILFILE *fp)
{
    // uint32_t vv;
    std::array<unsigned char, 4> buf;
    // size_t ret =
    VSIFReadL(&buf, 1, 4, fp);
    // if (ret<4) return (EOF);
    return (VRGetUInt(&buf, 0));
}

uint32_t VRReadUInt(VSILFILE *fp, unsigned int byteOffset)
{
    if (VSIFSeekL(fp, byteOffset, SEEK_SET))
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "VRReadInt cannot seek to VRC byteOffset %u=x%08x", byteOffset,
                 byteOffset);
        return CE_Failure;  // dangerous ?
    }
    return VRReadUInt(fp);
}

/*
 *               CRSfromCountry
 *
 */

// Some CRS use the "old" axis convention
#define VRC_SWAP_AXES poSRS->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER)

#define VRC_EPSG(A)                                                            \
    nEPSG = (A);                                                               \
    errImport = poSRS->importFromEPSGA(nEPSG)

extern OGRSpatialReference *CRSfromCountry(int16_t nCountry, int32_t nMapID)
{
    OGRErr errImport = OGRERR_NONE;
    int nEPSG = 0;

    auto *poSRS = new OGRSpatialReference();
    switch (nCountry)
    {
        case 0:  // Online maps
            break;
        case 1:  // UK Ordnace Survey
            VRC_EPSG(27700);
            break;
        case 2:  // Ireland.
            VRC_EPSG(29901);
            //  Could be 29901, 2 or 3
            break;
        case 5:  // Finland
            VRC_EPSG(2393);
            VRC_SWAP_AXES;
            break;
        case 8:  // Belgium, but some Belgium VRH (height) files are case 17:
            VRC_EPSG(31370);
            // Other possibilities for Belgium include
            //   EPSG:21500, EPSG:31300, EPSG:31370, EPSG:6190 and EPSG:3447.
            // BelgiumOverview.VRC is not EPSG:3812 or EPSG:4171
            break;
        case 9:  // Switzerland
            VRC_EPSG(21781);
            VRC_SWAP_AXES;
            break;
        case 12:  // Nederlands
            VRC_EPSG(28992);
            break;
        case 13:  // Slovenia
            VRC_EPSG(8677);
            // tbc
            break;
        case 14:  // Sweden SWEREF99
            VRC_EPSG(3006);
            VRC_SWAP_AXES;
            break;
        case 15:  // Norway
            VRC_EPSG(25833);
            break;
        case 16:  // Italy
            VRC_EPSG(32632);
            break;
        case 17:
            // This "country" code uses a different, unknown, unit - not metres.
            // USA, Discovery(Spain/Canaries/Greece)
            // and US + Belgium .VRH (height) files
            switch (nMapID)
            {
                case 0:
                    VRC_EPSG(4267);
                    VRC_SWAP_AXES;
                    break;
                default:
                    VRC_EPSG(4326);
                    VRC_SWAP_AXES;
                    break;
            }
            break;
        case 18:  // New Zealand
            VRC_EPSG(2193);
            VRC_SWAP_AXES;
            break;
        case 19:  // France
            VRC_EPSG(2154);
            break;
        case 20:  // Greece (also see 17 for Discovery Walking Guides)
            VRC_EPSG(2100);
            break;
        case 21:  // Spain (also see 17 for Discovery Walking Guides)
            VRC_EPSG(3042);
            VRC_SWAP_AXES;
            break;
        case 132:  // Austria/Germany/Denmark
            VRC_EPSG(25832);
            break;
        case 133:  // Czech Republic / Slovakia
            VRC_EPSG(32633);
            break;
        case 155:  // Australia
            // Note that in VRCDataset::GetGeoTransform()
            // we shift 10million metres north
            // (which undoes the false_northing).
            VRC_EPSG(28355);  // not VRC_EPSG(4283);
            break;
        default:
            CPLDebug("Viewranger",
                     "CRSfromCountry(country %hd unknown) assuming WGS 84",
                     nCountry);
            VRC_EPSG(4326);
            break;
    }

    if (errImport != OGRERR_NONE)
    {
        CPLDebug(
            "Viewranger",
            "failed to import EPSG:%d for CRSfromCountry(%hd, %d) error %d",
            nEPSG, nCountry, nMapID, errImport);
        delete poSRS;
        poSRS = nullptr;
    }

    return poSRS;
}

#undef VRC_EPSG
#undef VRC_SWAP_AXES

extern const char *CharsetFromCountry(int16_t nCountry)
{
    // CPLDebug("Viewranger", "CharsetFromCountry(%d)", nCountry);
    switch (nCountry)
    {
            // case 0: return ""; // Online maps
        case 1:  // UK Ordnance Survey
            return "LATIN9";
        case 2:  // Ireland
            return "LATIN9";
        case 5:  // Finland
            return "LATIN9";
        case 8:  // Belgium. Some Belgium .VRH files are case 17:
            return "LATIN9";
        case 9:  // Switzerland
            return "LATIN9";
        case 12:  // Nederlands
            return "LATIN9";
        case 13:  // Slovenia
            return "LATIN9";
        case 14:  // Sweden SWEREF99
            return "LATIN9";
        case 15:  // Norway
            return "LATIN9";
        case 16:  // Italy
            return "LATIN9";
        case 17:  // USA, Discovery(Spain/Canaries/Greece)
            // (US + Belgium .VRH files are also 17, but .VRH files have no strings).
            return "LATIN9";
        case 18:  // New Zealand
            return "LATIN9";
        case 19:  // France
            return "LATIN9";
        case 20:  // Greece
            // return "UTF-8";
            return "LATIN9";
        // case 21:  // Spain, but not Discovery Walking Guides ?
        //     return "UTF-8";
        case 132:  // Austria/Germany/Denmark
            return "LATIN9";
        case 133:  // Czech Republic / Slovakia
            return "LATIN9";
        case 155:  // Australia
            return "LATIN9";
        default:
            return "UTF-8";
    }
}
