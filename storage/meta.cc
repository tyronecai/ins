#include "meta.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>
#include "glog/logging.h"
#include "server/user_manage.h"
#include "utils.h"

namespace galaxy {
namespace ins {

const std::string term_file_name = "term.data";
const std::string vote_file_name = "vote.data";
const std::string root_file_name = "root.data";

Meta::Meta(const std::string& data_dir)
    : data_dir_(data_dir),
      term_file_(NULL),
      vote_file_(NULL),
      root_file_(NULL) {
  bool ok = ins_common::Mkdirs(data_dir.c_str());
  if (!ok) {
    LOG(FATAL) << "failed to create dir: " << data_dir;
  }
  LOG(INFO) << "open " << (data_dir + "/" + term_file_name)
             << " for term file";
  term_file_ = fopen((data_dir + "/" + term_file_name).c_str(), "a+");
  LOG(INFO) << "open " << (data_dir + "/" + vote_file_name)
             << " for vote file";
  vote_file_ = fopen((data_dir + "/" + vote_file_name).c_str(), "a+");
  LOG(INFO) << "open " << (data_dir + "/" + root_file_name)
             << " for root file";
  root_file_ = fopen((data_dir + "/" + root_file_name).c_str(), "r+");
  assert(term_file_ != NULL);
  assert(vote_file_ != NULL);
  if (root_file_ == NULL) {
    root_file_ = fopen((data_dir + "/" + root_file_name).c_str(), "w+");
    assert(root_file_);
  }
}

Meta::~Meta() {
  fclose(term_file_);
  fclose(vote_file_);
  fclose(root_file_);
}

int64_t Meta::ReadCurrentTerm() {
  int64_t cur_term = 0, tmp = 0;
  while (fscanf(term_file_, "%ld", &tmp) == 1) {
    cur_term = tmp;
  }
  LOG(INFO) << "got current term: " << cur_term;
  return cur_term;
}

void Meta::ReadVotedFor(std::map<int64_t, std::string>& voted_for) {
  voted_for.clear();
  int64_t term = 0;
  char server_id[1024] = {'\0'};
  int64_t last_term;
  std::string last_vote_for;
  while (fscanf(vote_file_, "%ld %1023s", &term, server_id) == 2) {
    last_term = term;
    last_vote_for = server_id;
  }
  if (!last_vote_for.empty()) {
    LOG(INFO) << "got last vote for: " << last_vote_for
               << " at term: " << last_term;
    voted_for[last_term] = std::string(last_vote_for);
  }
}

UserInfo Meta::ReadRootInfo() {
  char buf[1024] = {'\0'};
  UserInfo root;
  if (fgets(buf, 1023, root_file_) != NULL) {
    char* p;
    for (p = buf; *p != '\t'; ++p)
      ;
    *p++ = 0;
    root.set_username(buf);
    root.set_passwd(p);
    LOG(INFO) << "got username: " << buf << ", passwd: " << p << " for root";
  }
  return root;
}

void Meta::WriteCurrentTerm(int64_t current_term) {
  fprintf(term_file_, "%ld\n", current_term);
  if (fflush(term_file_) != 0) {
    LOG(FATAL) << "Meta::WriteCurrentTerm failed, term: " << current_term;
  }
}

void Meta::WriteVotedFor(int64_t term, const std::string& server_id) {
  fprintf(vote_file_, "%ld %s\n", term, server_id.c_str());
  if (fflush(vote_file_) != 0) {
    LOG(FATAL) << "Meta::WriteVotedFor failed, term: " << term
                << ", server_id: " << server_id;
  }
}

void Meta::WriteRootInfo(const UserInfo& user) {
  if (!user.has_username() || !user.has_passwd()) {
    return;
  }
  fseek(root_file_, 0, SEEK_SET);
  fprintf(root_file_, "%s\t%s\n", user.username().c_str(),
          user.passwd().c_str());
  if (fflush(root_file_) != 0) {
    LOG(FATAL) << "Meta::WriteUserList failed, username: " << user.username();
  }
}

}  // namespace ins
}  // namespace galaxy
