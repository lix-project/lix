nix_noinst_scripts := \
  $(d)/nix-profile.sh

noinst-scripts += $(nix_noinst_scripts)

profiledir = $(sysconfdir)/profile.d

$(eval $(call install-file-as, $(d)/nix-profile.sh, $(profiledir)/nix.sh, 0644))
$(eval $(call install-file-as, $(d)/nix-profile.fish, $(profiledir)/nix.fish, 0644))

clean-files += $(nix_noinst_scripts)
