---
synopsis: "Set `X-GitHub-Api-Version` header"
issues: [fj#255]
cls: [1925]
category: Development
credits: kiara
---

Sets the `X-GitHub-Api-Version` header to `2022-11-28` for calls to the
GitHub API.
This follows the later version as per
https://docs.github.com/en/rest/about-the-rest-api/api-versions?apiVersion=2022-11-28.

This affected the check on whether to use the API versus unauthenticated
calls as well, given the headers would no longer be empty if the
authentication token were missing.
The workaround to this used here is to use a check similar to an existing
check for the token.

In the current implementation, headers are (still) similarly sent to
non-authenticated as well as GitHub on-prem calls.
For what it's worth, manual curl calls with such a header seemed to
break nor unauthenticated calls nor ones to the github.com API.
