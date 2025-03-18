---
name: toXML
args: [e]
---
Return a string containing an XML representation of *e*. The main
application for `toXML` is to communicate information with the
builder in a more structured format than plain environment
variables.

Here is an example where this is the case:

```nix
{ stdenv, fetchurl, libxslt, jira, uberwiki }:

stdenv.mkDerivation (rec {
  name = "web-server";

  buildInputs = [ libxslt ];

  builder = builtins.toFile "builder.sh" "
    source $stdenv/setup
    mkdir $out
    echo "$servlets" | xsltproc ${stylesheet} - > $out/server-conf.xml ①
  ";

  stylesheet = builtins.toFile "stylesheet.xsl" ②
   "<?xml version='1.0' encoding='UTF-8'?>
    <xsl:stylesheet xmlns:xsl='http://www.w3.org/1999/XSL/Transform' version='1.0'>
      <xsl:template match='/'>
        <Configure>
          <xsl:for-each select='/expr/list/attrs'>
            <Call name='addWebApplication'>
              <Arg><xsl:value-of select=\"attr[@name = 'path']/string/@value\" /></Arg>
              <Arg><xsl:value-of select=\"attr[@name = 'war']/path/@value\" /></Arg>
            </Call>
          </xsl:for-each>
        </Configure>
      </xsl:template>
    </xsl:stylesheet>
  ";

  servlets = builtins.toXML [ ③
    { path = "/bugtracker"; war = jira + "/lib/atlassian-jira.war"; }
    { path = "/wiki"; war = uberwiki + "/uberwiki.war"; }
  ];
})
```

The builder is supposed to generate the configuration file for a
[Jetty servlet container](https://jetty.org/). A servlet
container contains a number of servlets (`*.war` files) each
exported under a specific URI prefix. So the servlet configuration
is a list of sets containing the `path` and `war` of the servlet
(①). This kind of information is difficult to communicate with the
normal method of passing information through an environment
variable, which just concatenates everything together into a
string (which might just work in this case, but wouldn’t work if
fields are optional or contain lists themselves). Instead the Nix
expression is converted to an XML representation with `toXML`,
which is unambiguous and can easily be processed with the
appropriate tools. For instance, in the example an XSLT stylesheet
(at point ②) is applied to it (at point ①) to generate the XML
configuration file for the Jetty server. The XML representation
produced at point ③ by `toXML` is as follows:

```xml
<?xml version='1.0' encoding='utf-8'?>
<expr>
  <list>
    <attrs>
      <attr name="path">
        <string value="/bugtracker" />
      </attr>
      <attr name="war">
        <path value="/nix/store/d1jh9pasa7k2...-jira/lib/atlassian-jira.war" />
      </attr>
    </attrs>
    <attrs>
      <attr name="path">
        <string value="/wiki" />
      </attr>
      <attr name="war">
        <path value="/nix/store/y6423b1yi4sx...-uberwiki/uberwiki.war" />
      </attr>
    </attrs>
  </list>
</expr>
```

Note that we used the `toFile` built-in to write the builder and
the stylesheet “inline” in the Nix expression. The path of the
stylesheet is spliced into the builder using the syntax `xsltproc
${stylesheet}`.
