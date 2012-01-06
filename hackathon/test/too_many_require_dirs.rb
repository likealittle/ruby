# When require 1K files from 1K diff directories, ruby slows down a lot.
# This happens because, ruby has a variable called $: which stores the ordered
# priority of directories to search for the required file. Ruby kernel currently
# loops through this array and checks one by one.

# If you fix this operation's complexity to O(1) or O(log(n)) by using a clever trie,
# then this test will pass. Make sure the trie handles the priority ordering though.

# To understand the $: variable, do the following:
# mkdir sample_dir
# touch sample_dir/sample_file.rb
# irb
# require "sample_file.rb" # fails with error.
# $: << "sample_dir" # ask ruby to look at the dir.
# require "sample_file.rb" # works.

class TooManyRequireDirsTest
  PATH = "./hackathon/test/gen"
  
  def main
    `mkdir -p #{PATH} &2>1`

    generate_same_dir

    start = Time.now
    run
    same_dir = Time.now - start
    puts "Requiring 1K files in the same directory ran in #{same_dir} seconds."

    generate_diff_dirs

    start = Time.now
    run
    diff_dirs = Time.now - start
    puts "Requiring 1K files from 1K diff require dirs ran in #{diff_dirs} seconds."

    if diff_dirs > 2*same_dir
      puts "FAILED: You have not successfully patched ruby yet. 1K directory requires take too long."
    else
      puts "PASSED."
    end
  end


  def write_file(path, contents)
    fh = File.new(path, 'w')
    fh.write(contents)
    fh.close
  end

  def generate_same_dir
    main = ""
    1000.times do |i|
      f = "%05d" % i
      main += "require '#{PATH}/#{f}'\n"
    end

    write_file("#{PATH}/main.rb",  main)
  end

  def generate_diff_dirs
    main = ""
    1000.times do |i|
      d = "d%05d" % i
      f = "%05d" % i
      Dir.mkdir(File.expand_path("#{PATH}/#{d}"), 0700) rescue nil
      write_file("#{PATH}/#{d}/#{f}.rb", "")
      main += "$: << '#{PATH}/#{d}'\n"
      main += "require '#{f}'\n"
    end

    write_file("#{PATH}/main.rb",  main)
  end

  def run
    res = `./ruby #{PATH}/main.rb &2>1`
    raise "Error: #{res}" if res != ""
  end
end

TooManyRequireDirsTest.new.main