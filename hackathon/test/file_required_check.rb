# When requiring 10K files ruby slows down like shit.
# Ruby before loading a file wants to make sure it's not already require'd.
# To do that it loops through a variable called $" and checks if this file is require'd.

# To understand the $" variable, do the following:
# touch sample_file.rb
# irb
# $"
# require "./sample_file.rb"
# $"
#
# You will now see that sample_file has been added to the set of all required files.

class FileRequiredTest
  PATH = "./hackathon/test/gen"
  
  def main
    `mkdir -p #{PATH} &2>1`

# generate_same_file

# start = Time.now
# run
# same_file = Time.now - start
# puts "Requiring the same file 10K times ran in #{same_file} seconds."

    generate_diff_files
    
    start = Time.now
    run
    diff_file = Time.now - start
    puts "Requiring 10K diff empty files ran in #{diff_file} seconds."

    if diff_file > 2*same_file
      puts "FAILED: You have not successfully patched ruby yet. 10K file requires take too long."
    else
      puts "PASSED."
    end
  end


  def write_file(path, contents)
    fh = File.new(path, 'w')
    fh.write(contents)
    fh.close
  end

  def generate_same_file
    main = ""
    10000.times do |i|
      f = "%05d" % 0
      main += "require '#{PATH}/#{f}'\n"
    end

    write_file("#{PATH}/main.rb",  main)
  end

  def generate_diff_files
    main = ""
    10000.times do |i|
      f = "%05d" % i
      write_file("#{PATH}/#{f}.rb", "")
      main += "require '#{PATH}/#{f}'\n"
    end

    write_file("#{PATH}/main.rb",  main)
  end

  def run
    res = `./ruby #{PATH}/main.rb &2>1`
    raise "Error: #{res}" if res != ""
  end

end

FileRequiredTest.new.main
