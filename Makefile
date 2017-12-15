SERVICE = codar.chimbuku.perf_anom.n_gram 
SERVICE_CAPS = N_GRAM
SCRIPTS_DIR = scripts
LIB_DIR = lib
TEST_DIR = test
DIR = $(shell pwd)
TEST_SCRIPT_NAME = run_tests.sh

.PHONY: test

default: build-test-script

build-test-script:
	echo '#!/bin/bash' > $(TEST_DIR)/$(TEST_SCRIPT_NAME)
	echo 'platform=`uname`' >> $(TEST_DIR)/$(TEST_SCRIPT_NAME)
	echo 'if [[ $$platform == "Darwin" ]]; then ' >> $(TEST_DIR)/$(TEST_SCRIPT_NAME)
	echo '    script_dir=$(DIR)/$(TEST_DIR)' >> $(TEST_DIR)/$(TEST_SCRIPT_NAME)
	echo 'else' >> $(TEST_DIR)/$(TEST_SCRIPT_NAME)
	echo '    script_dir=$$(dirname "$$(readlink -f "$$0")")' >> $(TEST_DIR)/$(TEST_SCRIPT_NAME)
	echo 'fi' >> $(TEST_DIR)/$(TEST_SCRIPT_NAME)
	echo 'export CODAR_DEPLOYMENT_CONFIG=$$script_dir/../deploy.cfg' >> $(TEST_DIR)/$(TEST_SCRIPT_NAME)
	echo 'export PYTHONPATH=$$script_dir/../$(LIB_DIR):$$PATH:$$PYTHONPATH' >> $(TEST_DIR)/$(TEST_SCRIPT_NAME)
	echo 'cd $$script_dir/../$(TEST_DIR)' >> $(TEST_DIR)/$(TEST_SCRIPT_NAME)
	echo 'python -m nose --with-coverage --cover-package=$(SERVICE) --cover-html --cover-html-dir=$$script_dir/../docs/test_coverage --nocapture  --nologcapture n_gram_test.py' >> $(TEST_DIR)/$(TEST_SCRIPT_NAME)
	chmod +x $(TEST_DIR)/$(TEST_SCRIPT_NAME)
	mkdir -p docs

test: build-test-script
	bash $(TEST_DIR)/$(TEST_SCRIPT_NAME)
