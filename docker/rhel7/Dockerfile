# Use CentOS 7 as the base image, which is the public equivalent of RHEL 7
FROM centos:7
RUN sed -i 's/mirrorlist/#mirrorlist/g' /etc/yum.repos.d/CentOS-* && \
    sed -i 's|#baseurl=http://mirror.centos.org/centos/$releasever|baseurl=http://vault.centos.org/7.9.2009|g' /etc/yum.repos.d/CentOS-*

ENV SCL_NAME devtoolset-8
ENV SCL_ENABLE /opt/rh/${SCL_NAME}/enable

# Define the build dependencies as listed in mysqlpcap.spec for RHEL 7, 
# plus the necessary rpm-build package.
# Note: We combine them into a single RUN layer to optimize image size.
RUN yum -y install centos-release-scl-rh
RUN ls /etc/yum.repos.d/
RUN sed -i 's/^mirrorlist/#mirrorlist/g' /etc/yum.repos.d/CentOS-SCLo-scl-rh.repo && \
    sed -i 's/^#baseurl=http:\/\/mirror\.centos\.org\/centos\/7/baseurl=http:\/\/vault.centos.org\/centos\/7/g' /etc/yum.repos.d/CentOS-SCLo-scl-rh.repo && \
    cat /etc/yum.repos.d/CentOS-SCLo-scl-rh.repo && \
    yum -y update  && \
    yum -y install \
        rpm-build rpmdevtools cmake \
        make \
        pkgconfig \
        zlib-devel openssl-devel bison ncurses-devel \
        libpcap-devel libaio-devel \
        pcre2-devel \
        wget \
        tar \
	devtoolset-8-gcc-c++ \
    && yum clean all

#ENTRYPOINT ["/bin/bash", "-c", "source /opt/rh/devtoolset-8/enable &&  cd /mysqlpcap && ./build-rpm.sh"]
