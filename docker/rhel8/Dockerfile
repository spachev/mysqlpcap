# Use CentOS 8 as the base image, which is the public equivalent of RHEL 7
FROM centos:8

# Define the build dependencies as listed in mysqlpcap.spec for RHEL 7, 
# plus the necessary rpm-build package.
# Note: We combine them into a single RUN layer to optimize image size.
RUN ls /etc/yum.repos.d/
RUN cat /etc/yum.repos.d/CentOS-Linux-AppStream.repo
RUN sed -i 's/mirrorlist/#mirrorlist/g' /etc/yum.repos.d/CentOS-*.repo && \
    sed -i 's/#baseurl/baseurl/g' /etc/yum.repos.d/CentOS-*.repo && \
    sed -i 's|baseurl=http://mirror.centos.org/$contentdir/$releasever|baseurl=http://vault.centos.org/8.5.2111|g' /etc/yum.repos.d/CentOS-*.repo && \
    sed -i 's/enabled=0/enabled=1/g' /etc/yum.repos.d/CentOS-Linux-{Plus,Devel,PowerTools}.repo

RUN cat /etc/yum.repos.d/CentOS-Linux-AppStream.repo
RUN yum -y update  && \
    yum -y install \
        rpm-build rpmdevtools cmake \
        make \
        pkgconfig \
        zlib-devel openssl-devel bison ncurses-devel \
        libpcap-devel libaio-devel \
        pcre2-devel \
        wget \
        tar \
        gcc-c++ \
    && yum clean all

#ENTRYPOINT ["/bin/bash", "-c", "source /opt/rh/devtoolset-8/enable &&  cd /mysqlpcap && ./build-rpm.sh"]
