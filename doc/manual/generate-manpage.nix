with builtins;

let
  splitLines = s: filter (x: !isList x) (split "\n" s);

  concatStrings = concatStringsSep "";

  replaceStringsRec =
    from: to: string:
    # recursively replace occurrences of `from` with `to` within `string`
    # example:
    #     replaceStringRec "--" "-" "hello-----world"
    #     => "hello-world"
    let
      replaced = replaceStrings [ from ] [ to ] string;
    in
    if replaced == string then string else replaceStringsRec from to replaced;

  squash = replaceStringsRec "\n\n\n" "\n\n";

  trim =
    string:
    # trim trailing spaces and squash non-leading spaces
    let
      trimLine =
        line:
        let
          # separate leading spaces from the rest
          parts = split "(^ *)" line;
          spaces = head (elemAt parts 1);
          rest = elemAt parts 2;
          # drop trailing spaces
          body = head (split " *$" rest);
        in
        spaces + replaceStringsRec "  " " " body;
    in
    concatStringsSep "\n" (map trimLine (splitLines string));

  # FIXME: O(n^2)
  unique = foldl' (acc: e: if elem e acc then acc else acc ++ [ e ]) [ ];

  nameValuePair = name: value: { inherit name value; };

  filterAttrs =
    pred: set:
    listToAttrs (
      concatMap (
        name:
        let
          v = set.${name};
        in
        if pred name v then [ (nameValuePair name v) ] else [ ]
      ) (attrNames set)
    );

  optionalString = cond: string: if cond then string else "";

  showSetting =
    { inlineHTML }:
    name:
    {
      description,
      documentDefault,
      defaultValue,
      aliases,
      value,
      experimentalFeature,
    }:
    let
      result = squash ''
        - ${
          if inlineHTML then ''<span id="conf-${name}">[`${name}`](#conf-${name})</span>'' else ''`${name}`''
        }

        ${indent "  " body}
      '';

      experimentalFeatureNote = optionalString (experimentalFeature != null) ''
        > **Warning**
        > This setting is part of an
        > [experimental feature](@docroot@/contributing/experimental-features.md).

        To change this setting, you need to make sure the corresponding experimental feature,
        [`${experimentalFeature}`](@docroot@/contributing/experimental-features.md#xp-feature-${experimentalFeature}),
        is enabled.
        For example, include the following in [`nix.conf`](#):

        ```
        extra-experimental-features = ${experimentalFeature}
        ${name} = ...
        ```
      '';

      # separate body to cleanly handle indentation
      body = ''
        ${description}

        ${experimentalFeatureNote}

        **Default:** ${showDefault documentDefault defaultValue}

        ${showAliases aliases}
      '';

      showDefault =
        documentDefault: defaultValue:
        if documentDefault then
          # a StringMap value type is specified as a string, but
          # this shows the value type. The empty stringmap is `null` in
          # JSON, but that converts to `{ }` here.
          if defaultValue == "" || defaultValue == [ ] || isAttrs defaultValue then
            "*empty*"
          else if isBool defaultValue then
            if defaultValue then "`true`" else "`false`"
          else
            "`${toString defaultValue}`"
        else
          "*machine-specific*";

      showAliases =
        aliases:
        optionalString (aliases != [ ])
          "**Deprecated alias:** ${(concatStringsSep ", " (map (s: "`${s}`") aliases))}";
    in
    result;

  indent =
    prefix: s: concatStringsSep "\n" (map (x: if x == "" then x else "${prefix}${x}") (splitLines s));

  showSettings =
    args: settingsInfo: concatStrings (attrValues (mapAttrs (showSetting args) settingsInfo));
in

inlineHTML: commandDump:

let

  commandInfo = fromJSON commandDump;

  showCommand =
    {
      command,
      details,
      filename,
      toplevel,
    }:
    let

      result = ''
        > **Warning** \
        > This program is
        > [**experimental**](@docroot@/contributing/experimental-features.md#xp-feature-nix-command)
        > and its interface is subject to change.

        # Name

        `${command}` - ${details.description}

        # Synopsis

        ${showSynopsis command details.args}

        ${maybeSubcommands}

        ${maybeStoreDocs}

        ${maybeOptions}
      '';

      showSynopsis =
        command: args:
        let
          showArgument = arg: "*${arg.label}*" + optionalString (!arg ? arity) "...";
          arguments = concatStringsSep " " (map showArgument args);
        in
        ''
          `${command}` [*option*...] ${arguments}
        '';

      maybeSubcommands = optionalString (details ? commands && details.commands != { }) ''
        where *subcommand* is one of the following:

        ${subcommands}
      '';

      subcommands = if length categories > 1 then listCategories else listSubcommands details.commands;

      categories = sort (x: y: x.id < y.id) (
        unique (map (cmd: cmd.category) (attrValues details.commands))
      );

      listCategories = concatStrings (map showCategory categories);

      showCategory = cat: ''
        **${toString cat.description}:**

        ${listSubcommands (filterAttrs (n: v: v.category == cat) details.commands)}
      '';

      listSubcommands = cmds: concatStrings (attrValues (mapAttrs showSubcommand cmds));

      showSubcommand = name: subcmd: ''
        * [`${command} ${name}`](./${appendName filename name}.md) - ${subcmd.description}
      '';

      # TODO: move this confusing special case out of here when implementing #8496
      maybeStoreDocs = optionalString (details ? doc) (
        replaceStrings [ "@stores@" ] [ storeDocs ] details.doc
      );

      maybeOptions = optionalString (details.flags != { }) ''
        # Options

        ${showOptions details.flags toplevel.flags}

        > **Note**
        >
        > See [`man nix.conf`](@docroot@/command-ref/conf-file.md#command-line-flags) for overriding configuration settings with command line flags.
      '';

      showOptions =
        options: commonOptions:
        let
          allOptions = options // commonOptions;
          showCategory = cat: ''
            ${optionalString (cat != "") "**${cat}:**"}

            ${listOptions (filterAttrs (n: v: v.category == cat && !v.hidden) allOptions)}
          '';
          listOptions = opts: concatStringsSep "\n" (attrValues (mapAttrs showOption opts));
          showOption =
            name: option:
            let
              result = trim ''
                - ${item}
                  ${option.description}
              '';
              item =
                if inlineHTML then
                  ''<span id="opt-${name}">[`--${name}`](#opt-${name})</span> ${shortName} ${labels}''
                else
                  "`--${name}` ${shortName} ${labels}";
              shortName = optionalString (option ? shortName) ("/ `-${option.shortName}`");
              labels = optionalString (option ? labels) (concatStringsSep " " (map (s: "*${s}*") option.labels));
            in
            result;
          categories = sort lessThan (unique (map (cmd: cmd.category) (attrValues allOptions)));
        in
        concatStrings (map showCategory categories);
    in
    squash result;

  appendName = filename: name: (if filename == "nix" then "nix3" else filename) + "-" + name;

  processCommand =
    {
      command,
      details,
      filename,
      toplevel,
    }:
    let
      cmd = {
        inherit command;
        name = filename + ".md";
        value = showCommand {
          inherit
            command
            details
            filename
            toplevel
            ;
        };
      };
      subcommand =
        subCmd:
        processCommand {
          command = command + " " + subCmd;
          details = details.commands.${subCmd};
          filename = appendName filename subCmd;
          inherit toplevel;
        };
    in
    [ cmd ] ++ concatMap subcommand (attrNames details.commands or { });

  manpages = processCommand {
    command = "nix";
    details = commandInfo.args;
    filename = "nix";
    toplevel = commandInfo.args;
  };

  storeDocs =
    let
      showStore =
        name:
        {
          settings,
          doc,
          experimentalFeature,
        }:
        let
          experimentalFeatureNote = optionalString (experimentalFeature != null) ''
            > **Warning**
            > This store is part of an
            > [experimental feature](@docroot@/contributing/experimental-features.md).

            To use this store, you need to make sure the corresponding experimental feature,
            [`${experimentalFeature}`](@docroot@/contributing/experimental-features.md#xp-feature-${experimentalFeature}),
            is enabled.
            For example, include the following in [`nix.conf`](@docroot@/command-ref/conf-file.md):

            ```
            extra-experimental-features = ${experimentalFeature}
            ```
          '';
        in
        ''
          ## ${name}

          ${doc}

          ${experimentalFeatureNote}

          **Settings**:

          ${showSettings { inherit inlineHTML; } settings}
        '';
    in
    concatStrings (attrValues (mapAttrs showStore commandInfo.stores));
in
listToAttrs manpages
