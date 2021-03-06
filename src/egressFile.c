#ifdef __linux__

#include "egressFile.h"

/* Plugin includes */

/* OLSRD includes */
#include "olsr_cfg.h"
#include "gateway_costs.h"
#include "gateway.h"
#include "scheduler.h"
#include "ipcalc.h"
#include "log.h"

/* System includes */
#include <unistd.h>
#include <fcntl.h>
#include <regex.h>
#include <sys/stat.h>
#include <assert.h>
#include <net/if.h>

/** the weights for the cost calculation */
static struct costs_weights gw_costs_weights_storage;
static struct costs_weights * gw_costs_weights = NULL;

/** regular expression describing a comment */
static const char * regexComment = "^([[:space:]]*|[[:space:]#]+.*)$";

/**
 * regular expression describing an egress line.
 *
 * # interface=uplink (Kbps),downlink (Kbps),path cost,gateway
 */
static const char * regexEgress = "^[[:space:]]*" //
        "([^[:space:]=]+)"                     /* 01: interface, mandatory, can NOT be empty */
        "[[:space:]]*=[[:space:]]*"            /* --: field separator */
        "([[:digit:]]*)"                       /* 02: uplink, mandatory, can be empty */
        "[[:space:]]*,[[:space:]]*"            /* --: field separator */
        "([[:digit:]]*)"                       /* 03: downlink, mandatory, can be empty */
        "("                                    /* 04: (the rest is optional) */
        "[[:space:]]*,[[:space:]]*"            /* --: field separator */
        "([[:digit:]]*)"                       /* 05: path cost, optional, can be empty */
        "("                                    /* 06: (the rest is optional) */
        "[[:space:]]*,[[:space:]]*"            /* --: field separator */
        "(|([[:digit:]\\.:]+)/([[:digit:]]+))" /* 07: network, optional, can be empty, 07=ip/x 08=ip 09=x */
        "("                                    /* 10: (the rest is optional) */
        "[[:space:]]*,[[:space:]]*"            /* --: field separator */
        "([[:digit:]\\.:]*)"                   /* 11: gateway, optional, can be empty */
        ")?"                                   /* 10 */
        ")?"                                   /* 06 */
        ")?"                                   /* 04 */
        "[[:space:]]*$";

/** the number of matches in regexEgress */
#define REGEX_EGRESS_LINE_MATCH_COUNT (1 /* 00 */ + 11)

/** the compiled regular expression describing a comment */
static regex_t compiledRegexComment;

/** the compiled regular expression describing an egress line */
static regex_t compiledRegexEgress;

/** true when the file reader has been started */
static bool started = false;

/** type to hold the cached stat result */
typedef struct _CachedStat {
#if defined(__linux__) && !defined(__ANDROID__)
  struct timespec timeStamp; /* Time of last modification (full resolution) */
#else
  time_t timeStamp; /* Time of last modification (second resolution) */
#endif
} CachedStat;

/** the cached stat result */
static CachedStat cachedStat;
static CachedStat cachedStatClear;

/** the malloc-ed buffer in which to store a line read from the file */
static char * line = NULL;

/** the maximum length of a line that is read from the file */
static size_t line_length = 256;

/* forward declaration */
static bool readEgressFile(const char * fileName);

/*
 * Error Reporting
 */

/** the maximum length of an error report */
#define ERROR_LENGTH 1024

/** true when errors have been reported, used to reduce error reports */
static bool reportedErrors = false;

/**
 * Report an error.
 *
 * @param useErrno
 * when true then errno is used in the error message; the error reason is also
 * reported.
 * @param lineNo
 * the line number of the caller
 * @param format
 * a pointer to the format string
 * @param ...
 * arguments to the format string
 */
__attribute__ ((format(printf, 3, 4)))
static void egressFileError(bool useErrno, int lineNo, const char *format, ...) {
  char str[ERROR_LENGTH];
  char *strErr = NULL;

  if (reportedErrors) {
    return;
  }

  if (useErrno) {
    strErr = strerror(errno);
  }

  if ((format == NULL ) || (*format == '\0')) {
    olsr_syslog(OLSR_LOG_ERR, "%s@%d: %s\n", __FILE__, lineNo, useErrno ? strErr : "Unknown error");
  } else {
    va_list arglist;

    va_start(arglist, format);
    vsnprintf(str, sizeof(str), format, arglist);
    va_end(arglist);

    str[sizeof(str) - 1] = '\0'; /* Ensures null termination */

    if (useErrno) {
      olsr_syslog(OLSR_LOG_ERR, "%s@%d: %s: %s\n", __FILE__, lineNo, str, strErr);
    } else {
      olsr_syslog(OLSR_LOG_ERR, "%s@%d: %s\n", __FILE__, lineNo, str);
    }
  }
}

/*
 * Helpers
 */

#ifdef __ANDROID__
static ssize_t getline(char **lineptr, size_t *n, FILE *stream)
{
    char *ptr;
    size_t len;

    ptr = fgetln(stream, n);

    if (ptr == NULL) {
        return -1;
    }

    /* Free the original ptr */
    if (*lineptr != NULL) free(*lineptr);

    /* Add one more space for '\0' */
    len = n[0] + 1;

    /* Update the length */
    n[0] = len;

    /* Allocate a new buffer */
    *lineptr = malloc(len);

    /* Copy over the string */
    memcpy(*lineptr, ptr, len-1);

    /* Write the NULL character */
    (*lineptr)[len-1] = '\0';

    /* Return the length of the new buffer */
    return len;
}
#endif

/**
 * Read an (olsr_ip_addr) IP address from a string:
 * First tries to parse the value as an IPv4 address, and if not successful
 * tries to parse it as an IPv6 address.
 *
 * @param str
 * The string to convert to an (olsr_ip_addr) IP address
 * @param dst
 * A pointer to the location where to store the (olsr_ip_addr) IP address upon
 * successful conversion. Not touched when errors are reported.
 * @param dstSet
 * A pointer to the location where to store the flag that signals whether the
 * IP address is set. Not touched when errors are reported.
 * @param dstIpVersion
 * A pointer to the location where to store the IP version of the IP address.
 * Not touched when errors are reported.
 *
 * @return
 * - true on success
 * - false otherwise
 */
static bool readIPAddress(const char * str, union olsr_ip_addr * dst, bool * dstSet, int * dstIpVersion) {
  int conversion;
  union olsr_ip_addr ip;
  int ip_version;

  assert(str);
  assert(dst);
  assert(dstSet);
  assert(dstIpVersion);

  /* try IPv4 first */
  ip_version = AF_INET;
  memset(&ip, 0, sizeof(ip));
  conversion = inet_pton(ip_version, str, &ip.v4);

  if (conversion != 1) {
    /* now try IPv6: IPv4 conversion was not successful */
    ip_version = AF_INET6;
    memset(&ip, 0, sizeof(ip));
    conversion = inet_pton(ip_version, str, &ip.v6);
  }

  if (conversion != 1) {
    return false;
  }

  *dst = ip;
  *dstSet = true;
  *dstIpVersion = ip_version;
  return true;
}

/**
 * Read an unsigned long long number from a value string.
 * An empty string results in a value of zero.
 *
 * @param value
 * The string to convert to a number
 * @param valueNumber
 * A pointer to the location where to store the number upon successful conversion.
 * Not touched when errors are reported.
 *
 * @return
 * - true on success
 * - false otherwise
 */
static bool readULL(const char * value, unsigned long long * valueNumber) {
  char * endPtr = NULL;
  unsigned long valueNew;

  assert(value);
  assert(valueNumber);

  if (!value || !strlen(value)) {
    *valueNumber = 0;
    return true;
  }

  errno = 0;
  valueNew = strtoull(value, &endPtr, 10);

  if (!((endPtr != value) && (*value != '\0') && (*endPtr == '\0')) || (errno == ERANGE)) {
    /* invalid conversion */
    return false;
  }

  *valueNumber = valueNew;
  return true;
}

/**
 * Strip EOL characters from the end of a string
 *
 * @param str the string to strip
 * @param length the length of the string
 */
static void stripEols(char * str, ssize_t length) {
  ssize_t len = length;
  while ((len > 0) && ((str[len - 1] == '\n') || (str[len - 1] == '\r'))) {
    len--;
  }
  str[len] = '\0';
}

/**
 * Find an egress interface in the configuration
 *
 * @param name the name of the egress interface
 * @return the pointer to the egress interface, NULL when not found
 */
struct sgw_egress_if * findEgressInterface(char * name) {
  if (name && (name[0] != '\0')) {
    struct sgw_egress_if * egress_if = olsr_cnf->smart_gw_egress_interfaces;
    while (egress_if) {
      if (!strcmp(egress_if->name, name)) {
        return egress_if;
      }
      egress_if = egress_if->next;
    }
  }

  return NULL ;
}

/**
 * Find an egress interface in the configuration by if_index
 *
 * @param if_index the index of the egress interface
 * @return the pointer to the egress interface, NULL when not found
 */
struct sgw_egress_if * findEgressInterfaceByIndex(int if_index) {
  if (if_index > 0) {
    struct sgw_egress_if * egress_if = olsr_cnf->smart_gw_egress_interfaces;
    while (egress_if) {
      if (egress_if->if_index == if_index) {
        return egress_if;
      }
      egress_if = egress_if->next;
    }
  }

  return NULL ;
}

/**
 * Calculate the costs from the bandwidth parameters
 *
 * @param bw the bandwidth parameters
 * @param up true when the interface is up
 * @return true when the costs changed
 */
bool egressBwCalculateCosts(struct egress_if_bw * bw, bool up) {
  if (!gw_costs_weights) {
    gw_costs_weights_storage.WexitU = olsr_cnf->smart_gw_weight_exitlink_up;
    gw_costs_weights_storage.WexitD = olsr_cnf->smart_gw_weight_exitlink_down;
    gw_costs_weights_storage.Wetx = olsr_cnf->smart_gw_weight_etx;
    gw_costs_weights_storage.Detx = olsr_cnf->smart_gw_divider_etx;
    gw_costs_weights = &gw_costs_weights_storage;
  }

  {
    int64_t costsPrevious = bw->costs;
    bw->costs = gw_costs_weigh(up, gw_costs_weights_storage, bw->path_cost, bw->egressUk, bw->egressDk);
    return (costsPrevious != bw->costs);
  }
}

/**
 * Clear the bandwidth parameters
 * @param bw the bandwidth parameters
 * @param up true when the interface is up
 */
void egressBwClear(struct egress_if_bw * bw, bool up) {
  bw->egressUk = 0;
  bw->egressDk = 0;
  bw->path_cost = 0;
  memset(&bw->network, 0, sizeof(bw->network));
  memset(&bw->gateway, 0, sizeof(bw->gateway));

  bw->networkSet = false;
  bw->gatewaySet = false;

  egressBwCalculateCosts(bw, up);
}

/*
 * Timer
 */

/** the timer for polling the egress file for changes */
static struct timer_entry *egress_file_timer;

/**
 * Timer callback to read the egress file
 *
 * @param unused unused
 */
static void egress_file_timer_callback(void *unused __attribute__ ((unused))) {
  if (readEgressFile(olsr_cnf->smart_gw_egress_file)) {
    doRoutesMultiGw(true, false, GW_MULTI_CHANGE_PHASE_RUNTIME);
  }
}

/*
 * Life Cycle
 */

/**
 * Initialises the egress file reader
 *
 * @return
 * - true upon success
 * - false otherwise
 */
bool startEgressFile(void) {
  int r;

  if (started) {
    return true;
  }

  line = malloc(line_length);
  if (!line) {
    egressFileError(false, __LINE__, "Could not allocate a line buffer");
    return false;
  }
  *line = '\0';

  r = regcomp(&compiledRegexComment, regexComment, REG_EXTENDED);
  if (r) {
    regerror(r, &compiledRegexComment, line, line_length);
    egressFileError(false, __LINE__, "Could not compile regex \"%s\" (%d = %s)", regexComment, r, line);

    free(line);
    line = NULL;
    return false;
  }

  r = regcomp(&compiledRegexEgress, regexEgress, REG_EXTENDED);
  if (r) {
    regerror(r, &compiledRegexEgress, line, line_length);
    egressFileError(false, __LINE__, "Could not compile regex \"%s\" (%d = %s)", regexEgress, r, line);

    regfree(&compiledRegexComment);
    free(line);
    line = NULL;
    return false;
  }

  memset(&cachedStat.timeStamp, 0, sizeof(cachedStat.timeStamp));
  memset(&cachedStatClear.timeStamp, 0, sizeof(cachedStatClear.timeStamp));

  readEgressFile(olsr_cnf->smart_gw_egress_file);

  olsr_set_timer(&egress_file_timer, olsr_cnf->smart_gw_egress_file_period, 0, true, &egress_file_timer_callback, NULL, NULL);

  started = true;
  return true;
}

/**
 * Cleans up the egress file reader.
 */
void stopEgressFile(void) {
  if (started) {
    olsr_stop_timer(egress_file_timer);
    egress_file_timer = NULL;

    regfree(&compiledRegexEgress);
    regfree(&compiledRegexComment);
    free(line);

    started = false;
  }
}

/*
 * File Reader
 */

/** the buffer with regex matches */
static regmatch_t pmatch[REGEX_EGRESS_LINE_MATCH_COUNT];

static void readEgressFileClear(void) {
  struct sgw_egress_if * egress_if = olsr_cnf->smart_gw_egress_interfaces;
  while (egress_if) {
    egress_if->bwPrevious = egress_if->bwCurrent;
    egress_if->bwCostsChanged = false;
    egress_if->bwNetworkChanged = false;
    egress_if->bwGatewayChanged = false;
    egress_if->bwChanged = false;

    egress_if->inEgressFile = false;

    egress_if = egress_if->next;
  }
}

/**
 * Read the egress file
 *
 * @param fileName the filename
 * @return true to indicate changes (any egress_if->bwChanged is true)
 */
static bool readEgressFile(const char * fileName) {
  bool changed = false;

  FILE * fp = NULL;
  struct stat statBuf;
  unsigned int lineNumber = 0;
  ssize_t length = -1;
  bool reportedErrorsLocal = false;
  const char * filepath = !fileName ? DEF_GW_EGRESS_FILE : fileName;
  void * mtim;

  if (memcmp(&cachedStat.timeStamp, &cachedStatClear.timeStamp, sizeof(cachedStat.timeStamp))) {
    /* read the file before */

    if (stat(filepath, &statBuf)) {
      /* could not stat the file */
      memset(&cachedStat.timeStamp, 0, sizeof(cachedStat.timeStamp));
      readEgressFileClear();
      goto outerror;
    }

#if defined(__linux__) && !defined(__ANDROID__)
    mtim = &statBuf.st_mtim;
#else
    mtim = &statBuf.st_mtime;
#endif
    if (!memcmp(&cachedStat.timeStamp, mtim, sizeof(cachedStat.timeStamp))) {
      /* file did not change since last read */
      return false;
    }
  }

  fp = fopen(filepath, "r");
  if (!fp) {
    /* could not open the file */
    memset(&cachedStat.timeStamp, 0, sizeof(cachedStat.timeStamp));
    readEgressFileClear();
    goto outerror;
  }

  /* copy 'current' egress interfaces into 'previous' field */
  readEgressFileClear();

  while ((length = getline(&line, &line_length, fp)) != -1) {
    struct sgw_egress_if * egress_if = NULL;
    unsigned long long uplink = DEF_EGRESS_UPLINK_KBPS;
    unsigned long long downlink = DEF_EGRESS_DOWNLINK_KBPS;
    unsigned long long pathCosts = DEF_EGRESS_PATH_COSTS;
    struct olsr_ip_prefix network;
    union olsr_ip_addr gateway;
    bool networkSet = false;
    bool gatewaySet = false;
    int networkIpVersion = AF_INET;
    int gatewayIpVersion = AF_INET;

    lineNumber++;

    if (!regexec(&compiledRegexComment, line, 0, NULL, 0)) {
      /* the line is a comment */
      continue;
    }

    memset(&network, 0, sizeof(network));
    memset(&gateway, 0, sizeof(gateway));

    stripEols(line, length);

    memset(pmatch, 0, sizeof(pmatch));
    if (regexec(&compiledRegexEgress, line, REGEX_EGRESS_LINE_MATCH_COUNT, pmatch, 0)) {
      egressFileError(false, __LINE__, "Egress speed file line %d uses invalid syntax: line is ignored (%s)", lineNumber, line);
      reportedErrorsLocal = true;
      continue;
    }

    /* iface: mandatory presence, guaranteed through regex match */
    {
      regoff_t len = pmatch[1].rm_eo - pmatch[1].rm_so;
      char * ifaceString = &line[pmatch[1].rm_so];
      line[pmatch[1].rm_eo] = '\0';

      if (len > IFNAMSIZ) {
        /* interface name is too long */
        egressFileError(false, __LINE__, "Egress speed file line %d: interface \"%s\" is too long: line is ignored", lineNumber, ifaceString);
        reportedErrorsLocal = true;
        continue;
      }

      egress_if = findEgressInterface(ifaceString);
      if (!egress_if) {
        /* not a known egress interface */
        egressFileError(false, __LINE__, "Egress speed file line %d: interface \"%s\" is not a configured egress interface: line is ignored", lineNumber,
            ifaceString);
        reportedErrorsLocal = true;
        continue;
      }
    }
    assert(egress_if);

    /* uplink: mandatory presence, guaranteed through regex match */
    {
      regoff_t len = pmatch[2].rm_eo - pmatch[2].rm_so;
      char * uplinkString = &line[pmatch[2].rm_so];
      line[pmatch[2].rm_eo] = '\0';

      if ((len > 0) && !readULL(uplinkString, &uplink)) {
        egressFileError(false, __LINE__, "Egress speed file line %d: uplink bandwidth \"%s\" is not a valid number: line is ignored", lineNumber, uplinkString);
        reportedErrorsLocal = true;
        continue;
      }
    }
    uplink = MIN(uplink, MAX_SMARTGW_SPEED);

    /* downlink: mandatory presence, guaranteed through regex match */
    {
      regoff_t len = pmatch[3].rm_eo - pmatch[3].rm_so;
      char * downlinkString = &line[pmatch[3].rm_so];
      line[pmatch[3].rm_eo] = '\0';

      if ((len > 0) && !readULL(downlinkString, &downlink)) {
        egressFileError(false, __LINE__, "Egress speed file line %d: downlink bandwidth \"%s\" is not a valid number: line is ignored", lineNumber,
            downlinkString);
        reportedErrorsLocal = true;
        continue;
      }
    }
    downlink = MIN(downlink, MAX_SMARTGW_SPEED);

    /* path costs: optional presence */
    if (pmatch[5].rm_so != -1) {
      regoff_t len = pmatch[5].rm_eo - pmatch[5].rm_so;
      char * pathCostsString = &line[pmatch[5].rm_so];
      line[pmatch[5].rm_eo] = '\0';

      if ((len > 0) && !readULL(pathCostsString, &pathCosts)) {
        egressFileError(false, __LINE__, "Egress speed file line %d: path costs \"%s\" is not a valid number: line is ignored", lineNumber, pathCostsString);
        reportedErrorsLocal = true;
        continue;
      }
    }
    pathCosts = MIN(pathCosts, UINT32_MAX);

    /* network: optional presence */
    if ((pmatch[7].rm_so != -1) && ((pmatch[7].rm_eo - pmatch[7].rm_so) > 0)) {
      /* network is present: guarantees IP and prefix presence */
      unsigned long long prefix_len;
      char * networkString = &line[pmatch[8].rm_so];
      char * prefixlenString = &line[pmatch[9].rm_so];
      line[pmatch[8].rm_eo] = '\0';
      line[pmatch[9].rm_eo] = '\0';

      if (!readIPAddress(networkString, &network.prefix, &networkSet, &networkIpVersion)) {
        egressFileError(false, __LINE__, "Egress speed file line %d: network IP address \"%s\" is not a valid IP address: line is ignored", lineNumber,
            networkString);
        reportedErrorsLocal = true;
        continue;
      }

      if (!readULL(prefixlenString, &prefix_len)) {
        egressFileError(false, __LINE__, "Egress speed file line %d: network prefix \"%s\" is not a valid number: line is ignored", lineNumber,
            prefixlenString);
        reportedErrorsLocal = true;
        continue;
      }

      if (prefix_len > ((networkIpVersion == AF_INET) ? 32 : 128)) {
        egressFileError(false, __LINE__, "Egress speed file line %d: network prefix \"%s\" is not in the range [0, %d]: line is ignored", lineNumber,
            prefixlenString, ((networkIpVersion == AF_INET) ? 32 : 128));
        reportedErrorsLocal = true;
        continue;
      }

      network.prefix_len = prefix_len;
    }

    /* gateway: optional presence */
    if (pmatch[11].rm_so != -1) {
      regoff_t len = pmatch[11].rm_eo - pmatch[11].rm_so;
      char * gatewayString = &line[pmatch[11].rm_so];
      line[pmatch[11].rm_eo] = '\0';

      if ((len > 0) && !readIPAddress(gatewayString, &gateway, &gatewaySet, &gatewayIpVersion)) {
        egressFileError(false, __LINE__, "Egress speed file line %d: gateway IP address \"%s\" is not a valid IP address: line is ignored", lineNumber,
            gatewayString);
        reportedErrorsLocal = true;
        continue;
      }
    }

    /* check all IP versions are the same */
    if ((networkSet && gatewaySet) && (networkIpVersion != gatewayIpVersion)) {
      egressFileError(false, __LINE__, "Egress speed file line %d: network and gateway IP addresses must be of the same IP version: line is ignored",
          lineNumber);
      reportedErrorsLocal = true;
      continue;
    }

    /* check no IPv6 */
    if ((networkSet && networkIpVersion == AF_INET6) || //
        (gatewaySet && gatewayIpVersion == AF_INET6)) {
      egressFileError(false, __LINE__, "Egress speed file line %d: network and gateway IP addresses must not be IPv6 addresses: line is ignored", lineNumber);
      reportedErrorsLocal = true;
      continue;
    }

    /* ensure network is masked by netmask */
    if (networkSet) {
      /* assumes IPv4 */
      in_addr_t mask = (network.prefix_len == 0) ? 0 : (~0U << (32 - network.prefix_len));
      uint32_t masked = ntohl(network.prefix.v4.s_addr) & mask;
      network.prefix.v4.s_addr = htonl(masked);
    }

    if (!uplink || !downlink) {
      egressBwClear(&egress_if->bwCurrent, egress_if->upCurrent);
    } else {
      egress_if->bwCurrent.egressUk = uplink;
      egress_if->bwCurrent.egressDk = downlink;
      egress_if->bwCurrent.path_cost = pathCosts;
      egress_if->bwCurrent.network = network;
      egress_if->bwCurrent.gateway = gateway;

      egress_if->bwCurrent.networkSet = networkSet;
      egress_if->bwCurrent.gatewaySet = gatewaySet;

      egressBwCalculateCosts(&egress_if->bwCurrent, egress_if->upCurrent);
    }

    egress_if->inEgressFile = true;
  }

  fclose(fp);
  fp = NULL;

#if defined(__linux__) && !defined(__ANDROID__)
    mtim = &statBuf.st_mtim;
#else
    mtim = &statBuf.st_mtime;
#endif
  memcpy(&cachedStat.timeStamp, mtim, sizeof(cachedStat.timeStamp));

  reportedErrors = reportedErrorsLocal;

  outerror:

  /* clear absent egress interfaces and setup 'changed' status */
  {
    struct sgw_egress_if * egress_if = olsr_cnf->smart_gw_egress_interfaces;
    while (egress_if) {
      if (!egress_if->inEgressFile) {
        egressBwClear(&egress_if->bwCurrent, egress_if->upCurrent);
      }

      egress_if->bwCostsChanged = egressBwCostsChanged(egress_if);
      egress_if->bwNetworkChanged = egressBwNetworkChanged(egress_if);
      egress_if->bwGatewayChanged = egressBwGatewayChanged(egress_if);
      egress_if->bwChanged = egressBwChanged(egress_if);
      if (egress_if->bwChanged) {
        changed = true;
      }

      egress_if = egress_if->next;
    }
  }

  return changed;
}

#endif /* __linux__ */
