Vagrant.configure(2) do |config|
  config.vm.define "deiso"
  config.vm.hostname = "deiso"
  config.vm.box = "ubuntu/trusty32"
  config.vm.provision :shell, path: "vagrantbootstrap.sh"
end
