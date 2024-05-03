ifeq ($(doc_generate),yes)

# The version of Nix used to generate the doc. Can also be
# `$(nix_INSTALL_PATH)` or just `nix` (to grap ambient from the `PATH`),
# if one prefers.
doc_nix = $(nix_PATH)

MANUAL_SRCS := \
	$(call rwildcard, $(d)/src, *.md) \
	$(call rwildcard, $(d)/src, */*.md)

man-pages := $(foreach n, \
	nix-env.1 nix-store.1 \
	nix-build.1 nix-shell.1 nix-instantiate.1 \
	nix-collect-garbage.1 \
	nix-prefetch-url.1 nix-channel.1 \
	nix-hash.1 nix-copy-closure.1 \
	nix.conf.5 nix-daemon.8 \
	nix-profiles.5 \
, doc/manual/generated/in/$(n))

# man pages for subcommands
# convert from `$(d)/src/command-ref/nix-{1}/{2}.md` to `$(d)/nix-{1}-{2}.1`
# FIXME: unify with how nix3-cli man pages are generated
man-pages += $(foreach subcommand, \
	$(filter-out %opt-common.md %env-common.md, $(wildcard $(d)/src/command-ref/nix-*/*.md)), \
	doc/manual/generated/in/$(subst /,-,$(subst $(d)/src/command-ref/,,$(subst .md,.1,$(subcommand)))))

clean-files += $(d)/*.1 $(d)/*.5 $(d)/*.8

# Provide a dummy environment for nix, so that it will not access files outside the macOS sandbox.
# Set cores to 0 because otherwise nix config show resolves the cores based on the current machine
dummy-env = env -i \
	HOME=/dummy \
	NIX_CONF_DIR=/dummy \
	NIX_SSL_CERT_FILE=/dummy/no-ca-bundle.crt \
	NIX_STATE_DIR=/dummy \
	NIX_CONFIG='cores = 0'

nix-eval = $(dummy-env) $(doc_nix) eval --experimental-features nix-command -I nix/corepkgs=corepkgs --store dummy:// --impure --raw

doc/manual/generated/in/nix-env-%.1: doc/manual/generated/out
	$(trace-gen) doc/manual/render-manpage.sh \
		--out-no-smarty "$(subst nix-env-,nix-env --,$$(basename "$@" .1))" 1 \
		doc/manual/generated/out/markdown/command-ref/nix-env/$*.md \
		$@

doc/manual/generated/in/nix-store-%.1: doc/manual/generated/out
	$(trace-gen) doc/manual/render-manpage.sh \
		--out-no-smarty "$(subst nix-store-,nix-store --,$$(basename "$@" .1))" 1 \
		doc/manual/generated/out/markdown/command-ref/nix-store/$*.md \
		$@


doc/manual/generated/in/%.1: doc/manual/generated/out
	$(trace-gen) doc/manual/render-manpage.sh "$$(basename $@ .1)" 1 \
		doc/manual/generated/out/markdown/command-ref/$*.md \
		$@

doc/manual/generated/in/%.8: doc/manual/generated/out
	$(trace-gen) doc/manual/render-manpage.sh "$$(basename $@ .8)" 8 \
		doc/manual/generated/out/markdown/command-ref/$*.md \
		$@

doc/manual/generated/in/nix.conf.5: doc/manual/generated/out
	$(trace-gen) doc/manual/render-manpage.sh "$$(basename $@ .5)" 5 \
		doc/manual/generated/out/markdown/command-ref/conf-file.md \
		$@

doc/manual/generated/in/nix-profiles.5: doc/manual/generated/out
	$(trace-gen) doc/manual/render-manpage.sh "$$(basename $@ .5)" 5 \
		doc/manual/generated/out/markdown/command-ref/files/profiles.md \
		$@

doc/manual/generated/in/command-ref/new-cli: doc/manual/generated/in/nix.json $(d)/utils.nix $(d)/generate-manpage.nix $(doc_nix)
	@mkdir -p doc/manual/generated/in/command-ref
	@rm -rf $@ $@.tmp
	$(trace-gen) $(nix-eval) --write-to $@.tmp --expr 'import doc/manual/generate-manpage.nix true (builtins.readFile $<)'
	@mv $@.tmp $@

doc/manual/generated/in/command-ref/conf-file.md: doc/manual/generated/in/conf-file.json $(d)/utils.nix doc/manual/generated/in/command-ref/experimental-features-shortlist.md $(doc_nix)
	@mkdir -p doc/manual/generated/in/command-ref
	$(trace-gen) $(nix-eval) --expr '(import doc/manual/utils.nix).showSettings { inlineHTML = true; } (builtins.fromJSON (builtins.readFile $<))' >> $@

doc/manual/generated/in/nix.json: $(doc_nix)
	@mkdir -p doc/manual/generated/in
	$(trace-gen) $(dummy-env) $(doc_nix) __dump-cli > $@.tmp
	@mv $@.tmp $@

doc/manual/generated/in/conf-file.json: $(doc_nix)
	@mkdir -p doc/manual/generated/in
	$(trace-gen) $(dummy-env) $(doc_nix) config show --json --experimental-features nix-command > $@.tmp
	@mv $@.tmp $@

doc/manual/generated/in/contributing/experimental-feature-descriptions.md: doc/manual/generated/in/xp-features.json $(d)/utils.nix $(d)/generate-xp-features.nix $(doc_nix)
	@mkdir -p doc/manual/generated/in/contributing
	@rm -rf $@ $@.tmp
	$(trace-gen) $(nix-eval) --write-to $@.tmp --expr 'import doc/manual/generate-xp-features.nix (builtins.fromJSON (builtins.readFile $<))'
	@mv $@.tmp $@

doc/manual/generated/in/command-ref/experimental-features-shortlist.md: doc/manual/generated/in/xp-features.json $(d)/utils.nix $(d)/generate-xp-features-shortlist.nix $(doc_nix)
	@mkdir -p doc/manual/generated/in/command-ref
	@rm -rf $@ $@.tmp
	$(trace-gen) $(nix-eval) --write-to $@.tmp --expr 'import doc/manual/generate-xp-features-shortlist.nix (builtins.fromJSON (builtins.readFile $<))'
	@mv $@.tmp $@

doc/manual/generated/in/xp-features.json: $(doc_nix)
	$(trace-gen) $(dummy-env) NIX_PATH=nix/corepkgs=corepkgs $(doc_nix) __dump-xp-features > $@.tmp
	@mv $@.tmp $@

doc/manual/generated/in/language/builtins.md: doc/manual/generated/in/language.json $(d)/generate-builtins.nix $(doc_nix)
	@mkdir -p doc/manual/generated/in/language
	$(trace-gen) $(nix-eval) --expr 'import doc/manual/generate-builtins.nix (builtins.fromJSON (builtins.readFile $<)).builtins' >> $@

doc/manual/generated/in/language/builtin-constants.md: doc/manual/generated/in/language.json $(d)/generate-builtin-constants.nix $(doc_nix)
	@mkdir -p doc/manual/generated/in/language
	$(trace-gen) $(nix-eval) --expr 'import doc/manual/generate-builtin-constants.nix (builtins.fromJSON (builtins.readFile $<)).constants' >> $@

doc/manual/generated/in/language.json: $(doc_nix)
	@mkdir -p doc/manual/generated/in
	$(trace-gen) $(dummy-env) NIX_PATH=nix/corepkgs=corepkgs $(doc_nix) __dump-language > $@.tmp
	@mv $@.tmp $@

# Generate "Upcoming release" notes (or clear it and remove from menu)
doc/manual/generated/in/release-notes/rl-next-generated.md: $(d)/rl-next $(d)/rl-next/*
	@mkdir -p doc/manual/generated/in/release-notes
	@if type -p build-release-notes > /dev/null; then \
		echo "  GEN   " $@; \
		build-release-notes doc/manual/rl-next > $@; \
	else \
		echo "  NULL  " $@; \
		true > $@; \
	fi

# Generate the HTML manual.
.PHONY: manual-html
manual-html: $(docdir)/manual/index.html
install: $(docdir)/manual/index.html

# Generate 'nix' manpages.
install: $(mandir)/man1/nix3-manpages
man: doc/manual/generated/man1/nix3-manpages
all: doc/manual/generated/man1/nix3-manpages

# FIXME: unify with how the other man pages are generated.
# this one works differently and does not use any of the amenities provided by `/mk/lib.mk`.
$(mandir)/man1/nix3-manpages: doc/manual/generated/man1/nix3-manpages
	@mkdir -p $(DESTDIR)$$(dirname $@)
	$(trace-install) install -m 0644 $$(dirname $<)/* $(DESTDIR)$$(dirname $@)

doc/manual/generated/man1/nix3-manpages: doc/manual/generated/out
	@mkdir -p $(DESTDIR)$$(dirname $@)
	$(trace-gen) for i in doc/manual/generated/out/markdown/command-ref/new-cli/*.md; do \
		name=$$(basename $$i .md); \
		tmpFile=$$(mktemp); \
		if [[ $$name = SUMMARY ]]; then continue; fi; \
		printf "Title: %s\n\n" "$$name" > $$tmpFile; \
		cat $$i >> $$tmpFile; \
		lowdown -sT man --nroff-nolinks -M section=1 $$tmpFile -o $(DESTDIR)$$(dirname $@)/$$name.1; \
		rm $$tmpFile; \
	done
	@touch $@

doc/manual/generated/out: $(MANUAL_SRCS) $(d)/book.toml $(d)/anchors.jq $(d)/custom.css $(d)/src/SUMMARY.md doc/manual/generated/in/command-ref/new-cli doc/manual/generated/in/command-ref/experimental-features-shortlist.md doc/manual/generated/in/contributing/experimental-feature-descriptions.md doc/manual/generated/in/command-ref/conf-file.md doc/manual/generated/in/language/builtins.md doc/manual/generated/in/language/builtin-constants.md doc/manual/generated/in/release-notes/rl-next-generated.md $(d)/substitute.py
	@rm -rf $@
	$(trace-gen) \
		MDBOOK_SUBSTITUTE_SEARCH=doc/manual/generated/in \
		RUST_LOG=warn \
		mdbook build doc/manual -d generated/out 2>&1 \
			| { grep -Fv "because fragment resolution isn't implemented" || :; }
	@find $@ -iname meson.build -delete

$(docdir)/manual/index.html: doc/manual/generated/out
	@mkdir -p $(DESTDIR)$(docdir)
	@rm -rf $(DESTDIR)$(docdir)/manual
	@cp -r $</html $(DESTDIR)$(docdir)/manual

endif
