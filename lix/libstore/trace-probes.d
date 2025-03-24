// I don't know how the rest of these stability attrs work and tbh I don't care
// either; these probes are not stable API and we don't need to be more
// specific about exactly how.
#pragma D attributes Unstable/Unstable/Common provider lix_store provider

provider lix_store {
    /** See filetransfer.cc; this is the consumption side, not the curl/production side. */
    probe filetransfer__read(string url, size_t length);
};
