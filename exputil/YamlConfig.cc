#include <YamlConfig.H>

void SaveConfig(const cxxopts::ParseResult& vm,  // Parsed options
		const cxxopts::Options& options, // Default options
		const std::string& config,       // Save file
		// Option groups to save, default by default
		const std::vector<std::string> groups,
		// Parameters to exclude
		const std::vector<std::string> exclude)
{    
  YAML::Emitter out;

  out << YAML::BeginMap;
  YAML::Node node;

  auto sep = std::string(20, '-');

  for (const auto kv: vm) {

    // Iterate through option groups
    for (auto g : options.groups()) {

      // Look for group in include list
      auto it = std::find(groups.begin(), groups.end(), g);

      // Found it: look for key in option list for this group
      if (it != groups.end()) {
	
	for (auto m : options.group_help(g).options) {
	  if (m.l == kv.key()) {
	    // Is this key in the excluded list?
	    auto jt = std::find(exclude.begin(), exclude.end(), kv.key());

	    // Not excluded: write to template file with desc as comments
	    if (jt == exclude.end()) {
	      // Write the key and then check the value
	      out << YAML::Key << kv.key();
	      // Are we a vector?
	      if (typeid(kv.value()) == typeid(std::vector<std::string>)) {
		out << YAML::BeginSeq;
		out << kv.value();
		out << YAML::EndSeq;
	      }
	      // We are a scalar
	      else {
		out << YAML::Value << kv.value();
	      }
	      out << YAML::Comment(m.desc);
	    }
	  }
	}
      }
    }
  }
  out << YAML::EndMap;
  
  std::ofstream temp(config);
  if (temp)
    temp << out.c_str();
  else
    std::cerr << "Could not save template file <" << config << ">"
	      << std::endl;
}


//! Read the YAML parameter config file and load the cxxopts database
//! with parameters
cxxopts::ParseResult LoadConfig(cxxopts::Options& options,
				const std::string& config)
{
  YAML::Node conf = YAML::LoadFile(config);

  int count = conf.size()*2+1, cnt = 1;
  char* data[count];

  data[0] = new char [11];
  strcpy(data[0], "LoadConfig"); // Emulate the caller name
  
  for (auto it=conf.begin(); it!=conf.end(); it++, cnt+=2) {
    std::ostringstream s1, s2;
    s1 << "--" << it->first.as<std::string>();
    
    // Are we vector valued?
    if (it->second.IsSequence()) {
      std::ostringstream sout;
      for (auto jt=it->second.begin(); jt!=it->second.end(); jt++) {
	if (jt != it->second.begin()) s2 << ','; // Add the comma
	s2 << jt->as<std::string>();		 // Add the element
      }
    }
    // We are scalar valued
    else {
      s2 << it->second.as<std::string>();
    }

    data[cnt+0] = new char [s1.str().size()+1];
    data[cnt+1] = new char [s2.str().size()+1];
    
    strcpy(data[cnt+0], s1.str().c_str());
    strcpy(data[cnt+1], s2.str().c_str());
  }

  auto vm = options.parse(count, &data[0]);

  for (auto & v : data) delete [] v;

  return vm;
}