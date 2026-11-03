#include "hash.h"
#include <string.h>

#define M32 0xFFFFFFFFUL

/* Generated tables: reflected polynomials 0xEDB88320 (CRC-32) and
 * 0x82F63B78 (CRC-32C). */
static const unsigned long hash_crc32_table[256] = {
    0x00000000UL, 0x77073096UL, 0xEE0E612CUL, 0x990951BAUL, 0x076DC419UL,
    0x706AF48FUL, 0xE963A535UL, 0x9E6495A3UL, 0x0EDB8832UL, 0x79DCB8A4UL,
    0xE0D5E91EUL, 0x97D2D988UL, 0x09B64C2BUL, 0x7EB17CBDUL, 0xE7B82D07UL,
    0x90BF1D91UL, 0x1DB71064UL, 0x6AB020F2UL, 0xF3B97148UL, 0x84BE41DEUL,
    0x1ADAD47DUL, 0x6DDDE4EBUL, 0xF4D4B551UL, 0x83D385C7UL, 0x136C9856UL,
    0x646BA8C0UL, 0xFD62F97AUL, 0x8A65C9ECUL, 0x14015C4FUL, 0x63066CD9UL,
    0xFA0F3D63UL, 0x8D080DF5UL, 0x3B6E20C8UL, 0x4C69105EUL, 0xD56041E4UL,
    0xA2677172UL, 0x3C03E4D1UL, 0x4B04D447UL, 0xD20D85FDUL, 0xA50AB56BUL,
    0x35B5A8FAUL, 0x42B2986CUL, 0xDBBBC9D6UL, 0xACBCF940UL, 0x32D86CE3UL,
    0x45DF5C75UL, 0xDCD60DCFUL, 0xABD13D59UL, 0x26D930ACUL, 0x51DE003AUL,
    0xC8D75180UL, 0xBFD06116UL, 0x21B4F4B5UL, 0x56B3C423UL, 0xCFBA9599UL,
    0xB8BDA50FUL, 0x2802B89EUL, 0x5F058808UL, 0xC60CD9B2UL, 0xB10BE924UL,
    0x2F6F7C87UL, 0x58684C11UL, 0xC1611DABUL, 0xB6662D3DUL, 0x76DC4190UL,
    0x01DB7106UL, 0x98D220BCUL, 0xEFD5102AUL, 0x71B18589UL, 0x06B6B51FUL,
    0x9FBFE4A5UL, 0xE8B8D433UL, 0x7807C9A2UL, 0x0F00F934UL, 0x9609A88EUL,
    0xE10E9818UL, 0x7F6A0DBBUL, 0x086D3D2DUL, 0x91646C97UL, 0xE6635C01UL,
    0x6B6B51F4UL, 0x1C6C6162UL, 0x856530D8UL, 0xF262004EUL, 0x6C0695EDUL,
    0x1B01A57BUL, 0x8208F4C1UL, 0xF50FC457UL, 0x65B0D9C6UL, 0x12B7E950UL,
    0x8BBEB8EAUL, 0xFCB9887CUL, 0x62DD1DDFUL, 0x15DA2D49UL, 0x8CD37CF3UL,
    0xFBD44C65UL, 0x4DB26158UL, 0x3AB551CEUL, 0xA3BC0074UL, 0xD4BB30E2UL,
    0x4ADFA541UL, 0x3DD895D7UL, 0xA4D1C46DUL, 0xD3D6F4FBUL, 0x4369E96AUL,
    0x346ED9FCUL, 0xAD678846UL, 0xDA60B8D0UL, 0x44042D73UL, 0x33031DE5UL,
    0xAA0A4C5FUL, 0xDD0D7CC9UL, 0x5005713CUL, 0x270241AAUL, 0xBE0B1010UL,
    0xC90C2086UL, 0x5768B525UL, 0x206F85B3UL, 0xB966D409UL, 0xCE61E49FUL,
    0x5EDEF90EUL, 0x29D9C998UL, 0xB0D09822UL, 0xC7D7A8B4UL, 0x59B33D17UL,
    0x2EB40D81UL, 0xB7BD5C3BUL, 0xC0BA6CADUL, 0xEDB88320UL, 0x9ABFB3B6UL,
    0x03B6E20CUL, 0x74B1D29AUL, 0xEAD54739UL, 0x9DD277AFUL, 0x04DB2615UL,
    0x73DC1683UL, 0xE3630B12UL, 0x94643B84UL, 0x0D6D6A3EUL, 0x7A6A5AA8UL,
    0xE40ECF0BUL, 0x9309FF9DUL, 0x0A00AE27UL, 0x7D079EB1UL, 0xF00F9344UL,
    0x8708A3D2UL, 0x1E01F268UL, 0x6906C2FEUL, 0xF762575DUL, 0x806567CBUL,
    0x196C3671UL, 0x6E6B06E7UL, 0xFED41B76UL, 0x89D32BE0UL, 0x10DA7A5AUL,
    0x67DD4ACCUL, 0xF9B9DF6FUL, 0x8EBEEFF9UL, 0x17B7BE43UL, 0x60B08ED5UL,
    0xD6D6A3E8UL, 0xA1D1937EUL, 0x38D8C2C4UL, 0x4FDFF252UL, 0xD1BB67F1UL,
    0xA6BC5767UL, 0x3FB506DDUL, 0x48B2364BUL, 0xD80D2BDAUL, 0xAF0A1B4CUL,
    0x36034AF6UL, 0x41047A60UL, 0xDF60EFC3UL, 0xA867DF55UL, 0x316E8EEFUL,
    0x4669BE79UL, 0xCB61B38CUL, 0xBC66831AUL, 0x256FD2A0UL, 0x5268E236UL,
    0xCC0C7795UL, 0xBB0B4703UL, 0x220216B9UL, 0x5505262FUL, 0xC5BA3BBEUL,
    0xB2BD0B28UL, 0x2BB45A92UL, 0x5CB36A04UL, 0xC2D7FFA7UL, 0xB5D0CF31UL,
    0x2CD99E8BUL, 0x5BDEAE1DUL, 0x9B64C2B0UL, 0xEC63F226UL, 0x756AA39CUL,
    0x026D930AUL, 0x9C0906A9UL, 0xEB0E363FUL, 0x72076785UL, 0x05005713UL,
    0x95BF4A82UL, 0xE2B87A14UL, 0x7BB12BAEUL, 0x0CB61B38UL, 0x92D28E9BUL,
    0xE5D5BE0DUL, 0x7CDCEFB7UL, 0x0BDBDF21UL, 0x86D3D2D4UL, 0xF1D4E242UL,
    0x68DDB3F8UL, 0x1FDA836EUL, 0x81BE16CDUL, 0xF6B9265BUL, 0x6FB077E1UL,
    0x18B74777UL, 0x88085AE6UL, 0xFF0F6A70UL, 0x66063BCAUL, 0x11010B5CUL,
    0x8F659EFFUL, 0xF862AE69UL, 0x616BFFD3UL, 0x166CCF45UL, 0xA00AE278UL,
    0xD70DD2EEUL, 0x4E048354UL, 0x3903B3C2UL, 0xA7672661UL, 0xD06016F7UL,
    0x4969474DUL, 0x3E6E77DBUL, 0xAED16A4AUL, 0xD9D65ADCUL, 0x40DF0B66UL,
    0x37D83BF0UL, 0xA9BCAE53UL, 0xDEBB9EC5UL, 0x47B2CF7FUL, 0x30B5FFE9UL,
    0xBDBDF21CUL, 0xCABAC28AUL, 0x53B39330UL, 0x24B4A3A6UL, 0xBAD03605UL,
    0xCDD70693UL, 0x54DE5729UL, 0x23D967BFUL, 0xB3667A2EUL, 0xC4614AB8UL,
    0x5D681B02UL, 0x2A6F2B94UL, 0xB40BBE37UL, 0xC30C8EA1UL, 0x5A05DF1BUL,
    0x2D02EF8DUL};

static const unsigned long hash_crc32c_table[256] = {
    0x00000000UL, 0xF26B8303UL, 0xE13B70F7UL, 0x1350F3F4UL, 0xC79A971FUL,
    0x35F1141CUL, 0x26A1E7E8UL, 0xD4CA64EBUL, 0x8AD958CFUL, 0x78B2DBCCUL,
    0x6BE22838UL, 0x9989AB3BUL, 0x4D43CFD0UL, 0xBF284CD3UL, 0xAC78BF27UL,
    0x5E133C24UL, 0x105EC76FUL, 0xE235446CUL, 0xF165B798UL, 0x030E349BUL,
    0xD7C45070UL, 0x25AFD373UL, 0x36FF2087UL, 0xC494A384UL, 0x9A879FA0UL,
    0x68EC1CA3UL, 0x7BBCEF57UL, 0x89D76C54UL, 0x5D1D08BFUL, 0xAF768BBCUL,
    0xBC267848UL, 0x4E4DFB4BUL, 0x20BD8EDEUL, 0xD2D60DDDUL, 0xC186FE29UL,
    0x33ED7D2AUL, 0xE72719C1UL, 0x154C9AC2UL, 0x061C6936UL, 0xF477EA35UL,
    0xAA64D611UL, 0x580F5512UL, 0x4B5FA6E6UL, 0xB93425E5UL, 0x6DFE410EUL,
    0x9F95C20DUL, 0x8CC531F9UL, 0x7EAEB2FAUL, 0x30E349B1UL, 0xC288CAB2UL,
    0xD1D83946UL, 0x23B3BA45UL, 0xF779DEAEUL, 0x05125DADUL, 0x1642AE59UL,
    0xE4292D5AUL, 0xBA3A117EUL, 0x4851927DUL, 0x5B016189UL, 0xA96AE28AUL,
    0x7DA08661UL, 0x8FCB0562UL, 0x9C9BF696UL, 0x6EF07595UL, 0x417B1DBCUL,
    0xB3109EBFUL, 0xA0406D4BUL, 0x522BEE48UL, 0x86E18AA3UL, 0x748A09A0UL,
    0x67DAFA54UL, 0x95B17957UL, 0xCBA24573UL, 0x39C9C670UL, 0x2A993584UL,
    0xD8F2B687UL, 0x0C38D26CUL, 0xFE53516FUL, 0xED03A29BUL, 0x1F682198UL,
    0x5125DAD3UL, 0xA34E59D0UL, 0xB01EAA24UL, 0x42752927UL, 0x96BF4DCCUL,
    0x64D4CECFUL, 0x77843D3BUL, 0x85EFBE38UL, 0xDBFC821CUL, 0x2997011FUL,
    0x3AC7F2EBUL, 0xC8AC71E8UL, 0x1C661503UL, 0xEE0D9600UL, 0xFD5D65F4UL,
    0x0F36E6F7UL, 0x61C69362UL, 0x93AD1061UL, 0x80FDE395UL, 0x72966096UL,
    0xA65C047DUL, 0x5437877EUL, 0x4767748AUL, 0xB50CF789UL, 0xEB1FCBADUL,
    0x197448AEUL, 0x0A24BB5AUL, 0xF84F3859UL, 0x2C855CB2UL, 0xDEEEDFB1UL,
    0xCDBE2C45UL, 0x3FD5AF46UL, 0x7198540DUL, 0x83F3D70EUL, 0x90A324FAUL,
    0x62C8A7F9UL, 0xB602C312UL, 0x44694011UL, 0x5739B3E5UL, 0xA55230E6UL,
    0xFB410CC2UL, 0x092A8FC1UL, 0x1A7A7C35UL, 0xE811FF36UL, 0x3CDB9BDDUL,
    0xCEB018DEUL, 0xDDE0EB2AUL, 0x2F8B6829UL, 0x82F63B78UL, 0x709DB87BUL,
    0x63CD4B8FUL, 0x91A6C88CUL, 0x456CAC67UL, 0xB7072F64UL, 0xA457DC90UL,
    0x563C5F93UL, 0x082F63B7UL, 0xFA44E0B4UL, 0xE9141340UL, 0x1B7F9043UL,
    0xCFB5F4A8UL, 0x3DDE77ABUL, 0x2E8E845FUL, 0xDCE5075CUL, 0x92A8FC17UL,
    0x60C37F14UL, 0x73938CE0UL, 0x81F80FE3UL, 0x55326B08UL, 0xA759E80BUL,
    0xB4091BFFUL, 0x466298FCUL, 0x1871A4D8UL, 0xEA1A27DBUL, 0xF94AD42FUL,
    0x0B21572CUL, 0xDFEB33C7UL, 0x2D80B0C4UL, 0x3ED04330UL, 0xCCBBC033UL,
    0xA24BB5A6UL, 0x502036A5UL, 0x4370C551UL, 0xB11B4652UL, 0x65D122B9UL,
    0x97BAA1BAUL, 0x84EA524EUL, 0x7681D14DUL, 0x2892ED69UL, 0xDAF96E6AUL,
    0xC9A99D9EUL, 0x3BC21E9DUL, 0xEF087A76UL, 0x1D63F975UL, 0x0E330A81UL,
    0xFC588982UL, 0xB21572C9UL, 0x407EF1CAUL, 0x532E023EUL, 0xA145813DUL,
    0x758FE5D6UL, 0x87E466D5UL, 0x94B49521UL, 0x66DF1622UL, 0x38CC2A06UL,
    0xCAA7A905UL, 0xD9F75AF1UL, 0x2B9CD9F2UL, 0xFF56BD19UL, 0x0D3D3E1AUL,
    0x1E6DCDEEUL, 0xEC064EEDUL, 0xC38D26C4UL, 0x31E6A5C7UL, 0x22B65633UL,
    0xD0DDD530UL, 0x0417B1DBUL, 0xF67C32D8UL, 0xE52CC12CUL, 0x1747422FUL,
    0x49547E0BUL, 0xBB3FFD08UL, 0xA86F0EFCUL, 0x5A048DFFUL, 0x8ECEE914UL,
    0x7CA56A17UL, 0x6FF599E3UL, 0x9D9E1AE0UL, 0xD3D3E1ABUL, 0x21B862A8UL,
    0x32E8915CUL, 0xC083125FUL, 0x144976B4UL, 0xE622F5B7UL, 0xF5720643UL,
    0x07198540UL, 0x590AB964UL, 0xAB613A67UL, 0xB831C993UL, 0x4A5A4A90UL,
    0x9E902E7BUL, 0x6CFBAD78UL, 0x7FAB5E8CUL, 0x8DC0DD8FUL, 0xE330A81AUL,
    0x115B2B19UL, 0x020BD8EDUL, 0xF0605BEEUL, 0x24AA3F05UL, 0xD6C1BC06UL,
    0xC5914FF2UL, 0x37FACCF1UL, 0x69E9F0D5UL, 0x9B8273D6UL, 0x88D28022UL,
    0x7AB90321UL, 0xAE7367CAUL, 0x5C18E4C9UL, 0x4F48173DUL, 0xBD23943EUL,
    0xF36E6F75UL, 0x0105EC76UL, 0x12551F82UL, 0xE03E9C81UL, 0x34F4F86AUL,
    0xC69F7B69UL, 0xD5CF889DUL, 0x27A40B9EUL, 0x79B737BAUL, 0x8BDCB4B9UL,
    0x988C474DUL, 0x6AE7C44EUL, 0xBE2DA0A5UL, 0x4C4623A6UL, 0x5F16D052UL,
    0xAD7D5351UL};

static unsigned long hash_rotl32(unsigned long x, unsigned int r) {
  x &= M32;
  return ((x << r) | (x >> (32U - r))) & M32;
}

static unsigned long hash_mul32(unsigned long a, unsigned long b) {
  return ((a & M32) * (b & M32)) & M32;
}

static unsigned long hash_read32(const unsigned char *p) {
  return (unsigned long)p[0] | ((unsigned long)p[1] << 8) |
         ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

unsigned long ds_hash_fnv1a32(const void *data, size_t length) {
  const unsigned char *p = (const unsigned char *)data;
  unsigned long h = 0x811C9DC5UL;
  size_t i;
  for (i = 0; i < length; i++) {
    h ^= p[i];
    h = hash_mul32(h, 0x01000193UL);
  }
  return h;
}

unsigned long ds_hash_cstr(const char *text) {
  return ds_hash_fnv1a32(text, strlen(text));
}

unsigned long ds_hash_mix32(unsigned long h) {
  h &= M32;
  h ^= h >> 16;
  h = hash_mul32(h, 0x85EBCA6BUL);
  h ^= h >> 13;
  h = hash_mul32(h, 0xC2B2AE35UL);
  h ^= h >> 16;
  return h;
}

unsigned long ds_hash_combine32(unsigned long hash, unsigned long value) {
  hash &= M32;
  hash ^=
      (ds_hash_mix32(value) + 0x9E3779B9UL + (hash << 6) + (hash >> 2)) & M32;
  return hash & M32;
}

unsigned long ds_hash_murmur3_32(const void *data, size_t length,
                                 unsigned long seed) {
  const unsigned char *p = (const unsigned char *)data;
  const unsigned long c1 = 0xCC9E2D51UL;
  const unsigned long c2 = 0x1B873593UL;
  unsigned long h = seed & M32;
  unsigned long k;
  size_t blocks = length / 4;
  size_t i;
  for (i = 0; i < blocks; i++) {
    k = hash_read32(p + 4 * i);
    k = hash_mul32(k, c1);
    k = hash_rotl32(k, 15);
    k = hash_mul32(k, c2);
    h ^= k;
    h = hash_rotl32(h, 13);
    h = (hash_mul32(h, 5UL) + 0xE6546B64UL) & M32;
  }
  k = 0;
  p += blocks * 4;
  if ((length & 3U) >= 3) {
    k ^= (unsigned long)p[2] << 16;
  }
  if ((length & 3U) >= 2) {
    k ^= (unsigned long)p[1] << 8;
  }
  if ((length & 3U) >= 1) {
    k ^= (unsigned long)p[0];
    k = hash_mul32(k, c1);
    k = hash_rotl32(k, 15);
    k = hash_mul32(k, c2);
    h ^= k;
  }
  h ^= (unsigned long)length & M32;
  return ds_hash_mix32(h);
}

#define XXH_P1 2654435761UL
#define XXH_P2 2246822519UL
#define XXH_P3 3266489917UL
#define XXH_P4 668265263UL
#define XXH_P5 374761393UL

static unsigned long hash_xxh_round(unsigned long v, unsigned long input) {
  v = (v + hash_mul32(input, XXH_P2)) & M32;
  v = hash_rotl32(v, 13);
  return hash_mul32(v, XXH_P1);
}

unsigned long ds_hash_xxh32(const void *data, size_t length,
                            unsigned long seed) {
  const unsigned char *p = (const unsigned char *)data;
  const unsigned char *end = p + length;
  unsigned long h;
  unsigned long v1;
  unsigned long v2;
  unsigned long v3;
  unsigned long v4;
  seed &= M32;
  if (length >= 16) {
    v1 = (seed + XXH_P1 + XXH_P2) & M32;
    v2 = (seed + XXH_P2) & M32;
    v3 = seed;
    v4 = (seed - XXH_P1) & M32;
    while ((size_t)(end - p) >= 16) {
      v1 = hash_xxh_round(v1, hash_read32(p));
      v2 = hash_xxh_round(v2, hash_read32(p + 4));
      v3 = hash_xxh_round(v3, hash_read32(p + 8));
      v4 = hash_xxh_round(v4, hash_read32(p + 12));
      p += 16;
    }
    h = (hash_rotl32(v1, 1) + hash_rotl32(v2, 7) + hash_rotl32(v3, 12) +
         hash_rotl32(v4, 18)) &
        M32;
  } else {
    h = (seed + XXH_P5) & M32;
  }
  h = (h + ((unsigned long)length & M32)) & M32;
  while ((size_t)(end - p) >= 4) {
    h = (h + hash_mul32(hash_read32(p), XXH_P3)) & M32;
    h = hash_mul32(hash_rotl32(h, 17), XXH_P4);
    p += 4;
  }
  while (p < end) {
    h = (h + hash_mul32(*p, XXH_P5)) & M32;
    h = hash_mul32(hash_rotl32(h, 11), XXH_P1);
    p++;
  }
  h ^= h >> 15;
  h = hash_mul32(h, XXH_P2);
  h ^= h >> 13;
  h = hash_mul32(h, XXH_P3);
  h ^= h >> 16;
  return h;
}

static unsigned long hash_crc(const unsigned long *table, unsigned long crc,
                              const void *data, size_t length) {
  const unsigned char *p = (const unsigned char *)data;
  size_t i;
  crc = (crc ^ M32) & M32;
  for (i = 0; i < length; i++) {
    crc = table[(crc ^ p[i]) & 0xFFUL] ^ (crc >> 8);
  }
  return (crc ^ M32) & M32;
}

unsigned long ds_crc32(unsigned long crc, const void *data, size_t length) {
  return hash_crc(hash_crc32_table, crc, data, length);
}

unsigned long ds_crc32c(unsigned long crc, const void *data, size_t length) {
  return hash_crc(hash_crc32c_table, crc, data, length);
}

unsigned long ds_adler32(unsigned long adler, const void *data, size_t length) {
  const unsigned char *p = (const unsigned char *)data;
  unsigned long a = adler & 0xFFFFUL;
  unsigned long b = (adler >> 16) & 0xFFFFUL;
  size_t chunk;
  while (length > 0) {
    /* 5552 is the largest n with 255n(n+1)/2 + (n+1)(65520) < 2^32. */
    chunk = length < 5552 ? length : 5552;
    length -= chunk;
    while (chunk > 0) {
      a += *p++;
      b += a;
      chunk--;
    }
    a %= 65521UL;
    b %= 65521UL;
  }
  return ((b << 16) | a) & M32;
}

unsigned int ds_fletcher16(const void *data, size_t length) {
  const unsigned char *p = (const unsigned char *)data;
  unsigned int sum1 = 0;
  unsigned int sum2 = 0;
  size_t i;
  for (i = 0; i < length; i++) {
    sum1 = (sum1 + p[i]) % 255U;
    sum2 = (sum2 + sum1) % 255U;
  }
  return (sum2 << 8) | sum1;
}
