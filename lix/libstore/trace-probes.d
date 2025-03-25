// I don't know how the rest of these stability attrs work and tbh I don't care
// either; these probes are not stable API and we don't need to be more
// specific about exactly how.

provider lix_store {
    /** See filetransfer.cc; this is the consumption side, not the curl/production side. */
    probe filetransfer__read(const char * url, size_t length);
};
#pragma D attributes Unstable/Unstable/Common provider lix_store provider
#pragma D attributes Unstable/Unstable/Common provider lix_store module
#pragma D attributes Unstable/Unstable/Common provider lix_store function
#pragma D attributes Unstable/Unstable/Common provider lix_store name
#pragma D attributes Unstable/Unstable/Common provider lix_store args
