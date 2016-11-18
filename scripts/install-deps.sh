su root

echo 'Begin to install epel.'

yum -y install epel-release.noarch

if [[ $? -ne 0 ]]; then
	echo 'Installsion for epel do not succeed.'
	exit $?	
fi

echo 'Begin to switch mirror.'
cp ./epel.repo /etc/yum.repos.d/epel.repo
echo 'Swith completed.'

echo 'Begin to install dependency.'
yum -y install automake autoconf libtool flex bison openssl-devel libxml2-devel python-devel libaio-devel libibverbs-devel librdmacm-devel readline-devel lvm2-devel glib2-devel userspace-rcu-devel libcmocka-devel libacl-devel sqlite-devel userspace-rcu-devel.x86_64

if [[ $? -ne 0 ]]; then
	echo 'Failed to install dependency.'
	exit $?
fi

echo 'Dependency installed successfully'
