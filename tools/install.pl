#!/usr/bin/perl -w

#Pragmas 
	use strict;

#Parse command line...
	my ($copy, $link);
	my $type = shift @ARGV;
	if( $type =~ /^copy$/i ) {
	  $copy = 1;
	} 
	if( $type =~ /^link$/i ) {
	  $link = 1;
	}

#Main program loop
	open (RPMOUT, "> filelist-rpm") or die;

	while( <> ){
		chomp;
		s/^\s*//;
		s/\s*$//;
		s/\s*#.*$//;
		next if m/^$/;
	
		my $line = $_;
		($line =~ m/^R...:(.*)/)  && print RPMOUT $1 . "\n";
		($line =~ m/^..M.:(.*)/)  && MakeDir( $1 );
		($line =~ m/^.C..:(.*)/)  && ($copy) && CopyFile( $1 );
		($line =~ m/^.C..:(.*)/)  && ($link) && LinkFile( $1 );
		($line =~ m/^R...:(.*)/)  && ChangeAttrs( $1 );
	}

sub CopyFile {
	my ($src, $dst) = ParseCLine( shift );

	if( -d $dst ) {
		$src =~ m|^.*/(.*)$|;
		my $file = $1;
		$dst .= "/" if ! ($dst=~m|/$|);
		$dst .= $file;
	}
	if( -e $dst ) {
		unlink $dst;
	}

	open INPUT, "<", $src or goto out1;
	open OUTPUT, ">", $dst or goto out2;
	while(<INPUT>){ print OUTPUT $_ }
	close OUTPUT;

	print "Installed File: $dst\n";
out2:
	close INPUT;
out1:	
	return;	
}

sub LinkFile {
	my ($src, $dst) = ParseCLine( shift );
	if( -d $dst ) {
		$src =~ m|^.*/(.*)$|;
		my $file = $1;
		$dst .= "/" if ! ($dst=~m|/$|);
		$dst .= $file;
	}
	if( -e $dst ) {
		unlink $dst;
	}
	symlink $src, $dst 
	   or warn "Linking $src to $dst failed $!\n";
	print "Installed File: $dst\n";
}

sub ParseCLine {
	my $line = shift;
	$line =~ s/^\s*//;
	$line =~ s/\s*$//;
	$line =~ m/^(.*?)\s+(.*?)$/;
	return ($1, $2);
}

sub ParseRLine {
	my $line = shift;
	my @directives;
	my @retval;
	$line =~ s/^\s*//;
	$line =~ s/\s*$//;
	my @words = split( /\s+/, $line );
	foreach my $word (@words) {
		if( $word =~ /^\%(.*)/ ) {
			push @directives, $word;
		} else {
			push @retval, $word;
		}
	}
	return \@retval, \@directives;
}

sub MakeDir {
	#R-M-: %attr(0755,root,ali) /opt/ali
	my $line = shift;

	#$line =~ m/\%attr\((.{1,5}),(\w+),(\w+)\)\s+(.*)/;
	my ($file_ref, $directive_ref) = ParseRLine( $line );
	
	my $dir = $file_ref->[0];

	if( ! -d $dir ) {
		mkdir $dir 
		  or warn "Make Dir: -->$dir<-- failed $!\n";
	}
	print "Made Dir: $dir\n";
}

sub GetUID {
	my $name = shift;
	my (undef, undef, $uid, undef) = getpwnam( $name ) ;
	$uid = defined($uid) ? $uid : -1;
	return $uid;
}

sub GetGID {
	my $name = shift;
	my (undef, undef, $gid, undef) = getgrnam( $name ) ;
	$gid = defined($gid) ? $gid : -1;
	return $gid;
}

sub ChangeAttrs {
	my $line = shift;

	my ($file_ref, $directive_ref) = ParseRLine( $line );
	my ($attr) = grep { /^\%attr/ } @$directive_ref;
	$attr =~ m/\%attr\((.{1,5}),(\w+),(\w+)\)/;
	my $perms = $1;
	my $owner = $2;
	my $group = $3;
	my $file = $file_ref->[0];

	my $uid = GetUID($owner);
	my $gid = GetGID($group); 

	chown $uid, $gid, $file;
	chmod oct($perms), $file;
}










