# --- CHECK CONFIG ---
$(if $(_gen_dir),,$(error Bad config))
$(if $(_gen_target_file),,$(error Bad config))
$(if $(_gen_command),,$(error Bad config))

# --- LOCATE FILES ---
override target := $(_gen_target_file)
override generator_source := $(_gen_dir)/$(_gen_source_file).cpp
override generator_binary := $(_gen_dir)/$(_gen_source_file)$(host_extension_exe)
override tmp_file := $(_gen_dir)/$(basename $(notdir $(target))).tmp$(suffix $(target))

override quote = '$(subst ','"'"',$1)'

# --- TARGETS
.PHONY: generate
generate: $(generator_source)
	@echo $(call quote,[Generating] $(target) [C++] $<)
	@$(_gen_command) $< -o $(call quote,$(generator_binary))
	@echo $(call quote,[Generating] $(target) [Running] $(generator_binary))
	@$(call quote,$(generator_binary)) $(call quote,$(tmp_file))
	@echo $(call quote,[Generating] $(target) [Validating] $(tmp_file))
	@$(_gen_command) -fsyntax-only -x c++-header $(call quote,$(tmp_file))
	@mv $(call quote,$(tmp_file)) $(call quote,$(target))
	@echo $(call quote,[Generating] $(target) [Done])
	@rm -f $(call quote,$(tmp_file)) $(call quote,$(generator_binary))
