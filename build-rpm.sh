#!/bin/bash
#
# Script: build_rpm.sh
# Description: Automates the process of creating a source tarball and building
#              the 'mysqlpcap' RPM package using rpmbuild.
#
# Prerequisites:
# 1. 'rpmbuild' tool is installed.
# 2. All source files (including CMakeLists.txt and .cc files) are in the
#    current directory.
# 3. A 'COPYING' or 'LICENSE' file is present as required by the spec file.
# 4. The 'mysqlpcap.spec' file is in the current directory.
# 5. Build dependencies (cmake, gcc-c++, mysql-devel, etc.) are installed
#    on the system where this script is run.
#
# Usage: ./build_rpm.sh
#

# --- Configuration ---
# Read Name, Version, and Source0 from the spec file
PACKAGE_NAME=$(grep -E '^Name:' mysqlpcap.spec | awk '{print $2}')
PACKAGE_VERSION=$(grep -E '^Version:' mysqlpcap.spec | awk '{print $2}')
SOURCE_ARCHIVE_NAME="${PACKAGE_NAME}-${PACKAGE_VERSION}.tar.gz"

# Define the RPM build environment structure
BUILD_ROOT="${HOME}/rpmbuild"
SOURCES_DIR="${BUILD_ROOT}/SOURCES"
SPECS_DIR="${BUILD_ROOT}/SPECS"
RPMS_DIR="${BUILD_ROOT}/RPMS"

# --- Functions ---

# Function to check for required tools
check_prerequisites() {
    echo "--- Checking Prerequisites ---"
    if ! command -v rpmbuild &> /dev/null; then
        echo "Error: 'rpmbuild' is not installed. Please install it (e.g., 'sudo yum install rpm-build')."
        exit 1
    fi

    if [ ! -f "mysqlpcap.spec" ]; then
        echo "Error: 'mysqlpcap.spec' not found in the current directory."
        exit 1
    fi

    # The spec file assumes a COPYING file exists for the license tag
    if [ ! -f "COPYING" ]; then
        echo "Warning: 'COPYING' file not found. The spec file requires it."
        echo "Creating a placeholder COPYING file now."
        echo "License details for mysqlpcap (GPLv2)" > COPYING
    fi
}

# Function to set up the build directory structure
setup_build_environment() {
    echo "--- Setting up RPM Build Environment ---"
    echo "Creating directory structure in: ${BUILD_ROOT}"
    mkdir -p "${SOURCES_DIR}" "${SPECS_DIR}" "${RPMS_DIR}/x86_64" "${BUILD_ROOT}/BUILD" "${BUILD_ROOT}/BUILDROOT"
}

# Function to create the source tarball (Source0)
create_source_tarball() {
    echo "--- Creating Source Tarball: ${SOURCE_ARCHIVE_NAME} ---"
    # The directory name inside the tarball MUST match the expectation in the spec file: %{name}-%{version}
    LOCAL_SOURCE_DIR="${PACKAGE_NAME}-${PACKAGE_VERSION}"

    # 1. Create a temporary directory to stage the source files
    rm -rf "/tmp/${LOCAL_SOURCE_DIR}"
    mkdir -p "/tmp/${LOCAL_SOURCE_DIR}"

    # 2. Copy necessary files into the staging area
    # Include all files needed to build the project
    echo "Copying source files to staging area..."
    cp -r CMakeLists.txt *.cc *.h COPYING "/tmp/${LOCAL_SOURCE_DIR}/" 2>/dev/null
    mkdir -p /tmp/${LOCAL_SOURCE_DIR}/cmake
    cp cmake/*.* /tmp/${LOCAL_SOURCE_DIR}/cmake
    # 3. Create the gzipped tarball
    tar -czf "${SOURCES_DIR}/${SOURCE_ARCHIVE_NAME}" -C /tmp "${LOCAL_SOURCE_DIR}"

    if [ $? -ne 0 ]; then
        echo "Error: Failed to create source tarball."
        exit 1
    fi

    # 4. Clean up the staging area
    rm -rf "/tmp/${LOCAL_SOURCE_DIR}"
    echo "Source tarball created at: ${SOURCES_DIR}/${SOURCE_ARCHIVE_NAME}"
}

die() {
  echo "$1"
  exit 1
}

# Function to copy the spec file
copy_spec_file() {
    echo "--- Copying Spec File ---"
    cp mysqlpcap.spec "${SPECS_DIR}/"
    echo "Spec file copied to: ${SPECS_DIR}/mysqlpcap.spec"
}

# Function to build the RPM
build_rpm() {
    echo "--- Building RPM Package ---"
    # Use the --define to specify the topdir, so rpmbuild doesn't rely solely on ~/.rpmmacros
    # Use -ba to build both binary and source RPMs (optional: use -bb for binary only)
    ver=$(grep mariadb_version mysqlpcap.spec| head -1| awk '{print $3}')
    curl -L -o /root/rpmbuild/SOURCES/mariadb-${ver}.tar.gz \
    https://archive.mariadb.org/mariadb-${ver}/source/mariadb-${ver}.tar.gz || die "MariaDB download failed"
    rpmbuild -bb \
        --define "_topdir ${BUILD_ROOT}" \
        "${SPECS_DIR}/mysqlpcap.spec"

    if [ $? -eq 0 ]; then
        echo ""
        echo "======================================================="
        echo "  SUCCESS: RPM build complete!"
        echo "======================================================="
        echo "Binary RPM location:"
        find "${RPMS_DIR}" -name "*.rpm"
        echo ""
        echo "Source RPM location:"
        find "${BUILD_ROOT}/SRPMS" -name "*.rpm"
        echo ""
    else
        echo ""
        echo "======================================================="
        echo "  FAILURE: RPM build failed. Check the logs above."
        echo "======================================================="
        exit 1
    fi
}

# --- Main Execution ---
check_prerequisites
setup_build_environment
create_source_tarball
copy_spec_file
build_rpm

exit 0

